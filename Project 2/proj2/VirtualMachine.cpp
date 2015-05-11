#include "VirtualMachine.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <iostream>
#include "Machine.h"
#include <vector>
#include <queue>

using namespace std;


#define SMALL_BUFFER_SIZE       256
typedef void (*TVMThreadEntry)(void *); 

extern "C"{

	volatile int tickCounter;
	volatile TVMThreadID gID = 0;
	


	struct Thread{
		TVMThreadID tID;
		TVMThreadState tState;
		TVMThreadPriority tPriority;
		TVMMemorySize tmemSize;
		uint8_t* sPoint;
		TVMThreadEntry tEntry;
		void * eParam; 
		SMachineContext mContext; 
		int tTick;
		jmp_buf DJumpBuffer;
	};

	struct fileS{
		TVMThreadID fID;
		int fileData;
	};

	struct mutex{
		int semaphore;
		TVMMutexID mRef;
		int id;

		std::queue<Thread*> mqueueL;
		std::queue<Thread*> mqueueN;
		std::queue<Thread*> mqueueH;
		
	};

	std::vector<mutex*> mVector;

	std::vector<Thread*> tvectorT;

	std::queue<Thread*> LqueueR;
	std::queue<Thread*> NqueueR;
	std::queue<Thread*> HqueueR;

	std::vector<Thread*> sleepVector;

	void *VMLibraryHandle = NULL;

	TVMMainEntry VMLoadModule(const char *module){

		VMLibraryHandle = dlopen(module, RTLD_NOW);
		if(NULL == VMLibraryHandle){
			fprintf(stderr,"Error dlopen failed %s\n",dlerror());
			return NULL;
		}

		return (TVMMainEntry)dlsym(VMLibraryHandle, "VMMain");
	}

	void VMUnloadModule(void){
		if(NULL != VMLibraryHandle){
			dlclose(VMLibraryHandle);
		}
		VMLibraryHandle = NULL;
	}

	void Scheduler(){
		//<< "gID: " << gID << endl;
		if(HqueueR.empty() != true){
			//(1, "high", 4);
			unsigned int tempID;
			tempID = HqueueR.front()->tID;
			HqueueR.front()->tState = VM_THREAD_STATE_READY;
			HqueueR.pop();
			unsigned int x = gID;
			gID = tempID;
			//<< "tempID:" << tempID << endl;
			tvectorT.at(tempID)->tState = VM_THREAD_STATE_RUNNING;
			MachineContextSwitch(&(tvectorT.at(x)->mContext), &(tvectorT.at(tempID)->mContext));
		}
		else if(NqueueR.empty() != true){
			unsigned int tempID;
			tempID = NqueueR.front()->tID;
			NqueueR.front()->tState = VM_THREAD_STATE_READY;
			NqueueR.pop();
			unsigned int x = gID;
			gID = tempID;
			tvectorT.at(tempID)->tState = VM_THREAD_STATE_RUNNING;
			//<< "tempID:" << tempID << endl;
			// << "schedule: " <<tempID << endl;
			MachineContextSwitch(&(tvectorT.at(x)->mContext), &(tvectorT.at(tempID)->mContext));
		}
		else if(LqueueR.empty() != true){
			//(1, "low", 3);
			unsigned int tempID;
			tempID = LqueueR.front()->tID;
			LqueueR.front()->tState = VM_THREAD_STATE_READY;
			LqueueR.pop();
			unsigned int x = gID;
			gID = tempID;
			//<< "tempID:" << tempID << endl;
			tvectorT.at(tempID)->tState = VM_THREAD_STATE_RUNNING;
			MachineContextSwitch(&(tvectorT.at(x)->mContext), &(tvectorT.at(tempID)->mContext));
			
		}
		else{
			unsigned int x = gID;
			gID = 1;
			tvectorT.at(1)->tState = VM_THREAD_STATE_RUNNING;
			//<< "idle: 1" << endl; 
			MachineContextSwitch(&(tvectorT.at(x)->mContext), &(tvectorT.at(1)->mContext));
			
		}

	}

	void AlarmCallback(void *param){
		TMachineSignalState SState;
		MachineSuspendSignals(&SState);

		for(unsigned int i = 0; i < sleepVector.size(); i++){
			if(sleepVector[i]->tTick >= 1){
				sleepVector[i]->tTick = sleepVector[i]->tTick - 1;
				
				if(sleepVector[i]->tTick < 1){
					TVMThreadID tempID = sleepVector[i]->tID;
					sleepVector.erase(sleepVector.begin()+i);
					tvectorT.at(tempID)->tState = VM_THREAD_STATE_READY;
					if(tvectorT.at(tempID)->tPriority == VM_THREAD_PRIORITY_HIGH){
						HqueueR.push(tvectorT.at(tempID));
					}
					else if(tvectorT.at(tempID)->tPriority == VM_THREAD_PRIORITY_NORMAL){
						NqueueR.push(tvectorT.at(tempID));
					}
					else if(tvectorT.at(tempID)->tPriority == VM_THREAD_PRIORITY_LOW){
						LqueueR.push(tvectorT.at(tempID));
					}
					if(tvectorT.at(tempID)->tPriority > tvectorT.at(gID)->tPriority){
						tvectorT.at(gID)->tState = VM_THREAD_STATE_READY;
						if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_LOW){
							LqueueR.push(tvectorT.at(gID));
						}
						else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_NORMAL){
							NqueueR.push(tvectorT.at(gID));
						}
						else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_HIGH){
							HqueueR.push(tvectorT.at(gID));
						}
						Scheduler();
					}
				}
				
			}
		}
		MachineEnableSignals();
	}

	TVMStatus VMFilePrint(int filedescriptor, const char *format, ...){
		va_list ParamList;
		char *OutputBuffer;
		char SmallBuffer[SMALL_BUFFER_SIZE];
		int SizeRequired;
		TVMStatus ReturnValue;

		va_start(ParamList, format);
		OutputBuffer = SmallBuffer;

		SizeRequired = vsnprintf(OutputBuffer, SMALL_BUFFER_SIZE, format, ParamList);
		if(SizeRequired < SMALL_BUFFER_SIZE){
			ReturnValue = VMFileWrite(filedescriptor, OutputBuffer, &SizeRequired);
			return ReturnValue;
		}
		OutputBuffer = (char *)malloc(sizeof(char) *(SizeRequired + 1));
		SizeRequired = vsnprintf(OutputBuffer, SizeRequired, format, ParamList);
		ReturnValue = VMFileWrite(filedescriptor, OutputBuffer, &SizeRequired);
		free(OutputBuffer);
		return ReturnValue;
	}

	void idleBusy(void* param){

		while(1){

		}
	}
	

	TVMStatus VMStart(int tickms, int machinetickms, int argc, char *argv[]){
		typedef void(*TVMMain)(int argc, char* argv[]);
		TVMMain vmmain;
//    // << argv[0][0];
		MachineInitialize(machinetickms);
		MachineRequestAlarm(tickms*1000, AlarmCallback, NULL);
		Thread * mainThread = new Thread;
		mainThread->tID = 0;
		mainThread->tState = VM_THREAD_STATE_RUNNING;
		mainThread->tPriority = VM_THREAD_PRIORITY_NORMAL;
		mainThread->tTick = 0;
		tvectorT.push_back(mainThread);

		TVMThreadID tempID;
		VMThreadCreate(idleBusy, NULL, 0x100000, 0x00, &tempID);
		VMThreadActivate(tempID);

		vmmain = VMLoadModule(argv[0]);
		MachineEnableSignals();
		vmmain(argc, argv);
		MachineTerminate();
		return VM_STATUS_SUCCESS;
	}



	void FileCallback(void *param, int x){
		MachineEnableSignals();
	//	TMachineSignalState SState;
		// << "x: " << x << endl;
		// << "filecallback" << endl;
		fileS fs = *(static_cast <fileS*> (param));
		(static_cast <fileS*> (param))->fileData = x;
		//MachineSuspendSignals(&SState);
		int y = fs.fID;
		
		// << "gID" << gID << "y: " << y <<  endl; 
		tvectorT.at(y)->tState = VM_THREAD_STATE_READY;
		NqueueR.push(tvectorT.at(y));
		if(tvectorT.at(y)->tPriority > tvectorT.at(gID)->tPriority){
			tvectorT.at(gID)->tState = VM_THREAD_STATE_READY;
			if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_LOW){
				LqueueR.push(tvectorT.at(gID));
			}
			else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_NORMAL){
				NqueueR.push(tvectorT.at(gID));
			}
			else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_HIGH){
				HqueueR.push(tvectorT.at(gID));
			}
			Scheduler();
		}
	}	

	TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
//		//(filedescriptor, data, *length);
		TMachineSignalState SState;
		MachineSuspendSignals(&SState);

		TMachineFileCallback callback = FileCallback;
		fileS fs;
		fs.fID = gID;


		MachineFileWrite(filedescriptor, data, *length, callback, &fs);
		tvectorT.at(gID)->tState = VM_THREAD_STATE_WAITING;
		Scheduler();
		*length = fs.fileData;
		MachineResumeSignals(&SState);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
		TMachineSignalState SState;
		MachineSuspendSignals(&SState);
		
		fileS fs;
		fs.fID = gID;


		MachineFileOpen(filename, flags, mode, FileCallback, &fs);
		// << "gID: " << gID << endl;	
		tvectorT.at(gID)->tState = VM_THREAD_STATE_WAITING;
		Scheduler();
		*filedescriptor = fs.fileData;
		MachineResumeSignals(&SState);
		// << "hi" << endl;
		if(filename == NULL || filedescriptor == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		else if(*filedescriptor == 3){
			return VM_STATUS_SUCCESS;
		}
		else{
			return VM_STATUS_FAILURE;
		}
	}

	TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){
		TMachineSignalState SState;
		MachineSuspendSignals(&SState);

		
		fileS fs;
		fs.fID = gID;
		MachineFileSeek(filedescriptor, offset, whence, FileCallback, &fs);
		
		tvectorT.at(gID)->tState = VM_THREAD_STATE_WAITING;
		Scheduler();
		*newoffset = fs.fileData;
		MachineResumeSignals(&SState);	
		if(*newoffset == 6){		
			return VM_STATUS_SUCCESS;
		}
		else{
			return VM_STATUS_FAILURE;
		}
	}

	TVMStatus VMFileClose(int filedescriptor){
		TMachineSignalState SState;
		MachineSuspendSignals(&SState);
		
		TMachineFileCallback callback = FileCallback;

		fileS fs;
		fs.fID = gID;
		
		MachineFileClose(filedescriptor, callback, &fs);
		tvectorT.at(gID)->tState = VM_THREAD_STATE_WAITING;
		Scheduler();

		MachineResumeSignals(&SState);
		return VM_STATUS_SUCCESS;	
	}

	TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
		TMachineSignalState SState;
		MachineSuspendSignals(&SState);

		fileS fs;
		fs.fID = gID;
		MachineFileRead(filedescriptor, data, *length, FileCallback, &fs);
		tvectorT.at(gID)->tState = VM_THREAD_STATE_WAITING;
		Scheduler();
		*length = fs.fileData;
		MachineResumeSignals(&SState);	
		if(data == NULL || length == NULL){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		else if(*length == 6){
			return VM_STATUS_SUCCESS;
		}
		else{
			return VM_STATUS_FAILURE;
		}
	}

	TVMStatus VMThreadSleep(TVMTick tick){
		if(tick == VM_TIMEOUT_INFINITE){
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		else{
			tvectorT.at(gID)->tTick = tick;
			tvectorT.at(gID)->tState = VM_THREAD_STATE_WAITING;
			sleepVector.push_back(tvectorT.at(gID));
			MachineEnableSignals();

			Scheduler();
			return VM_STATUS_SUCCESS;
		}
	}


TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
	TMachineSignalState SState;
	MachineSuspendSignals(&SState);

	struct Thread * initThread = new Thread;
	initThread->tEntry = entry;
	initThread->eParam = param;
	initThread->tPriority = prio;
	initThread->tmemSize = memsize;
	initThread->tTick = 0;
	initThread->sPoint = new uint8_t[memsize];

	initThread->tID = tvectorT.size();
	*tid = tvectorT.size();

	if (entry == NULL || tid == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	else{
		tvectorT.push_back(initThread);
	}
	MachineResumeSignals(&SState);

	return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef state){

	if (tvectorT.size() < thread)
	{
		return VM_STATUS_ERROR_INVALID_ID;
	}
	else if (state == NULL)
	{
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	else{
		* state = tvectorT.at(thread)->tState;
		return VM_STATUS_SUCCESS;
	}
}

void Skeleton(void* param){

	unsigned int x = *((unsigned int*) param);
	TVMThreadID threadID ;
	threadID =  x;
	MachineEnableSignals();

	tvectorT.at(threadID)->tEntry(tvectorT.at(threadID)->eParam);

	VMThreadTerminate(tvectorT.at(threadID)->tID);
}



TVMStatus VMThreadActivate(TVMThreadID thread){
	TMachineSignalState SState;
	MachineSuspendSignals(&SState);
	void * tempParam = (void*) (&(tvectorT.at(thread)->tID));

	MachineContextCreate(&(tvectorT.at(thread)->mContext), Skeleton, tempParam, tvectorT.at(thread)->sPoint, tvectorT.at(thread)->tmemSize);


	tvectorT.at(thread)->tState = VM_THREAD_STATE_READY;

	if(tvectorT.at(thread)->tPriority == VM_THREAD_PRIORITY_LOW){
		LqueueR.push(tvectorT.at(thread));
	}
	else if(tvectorT.at(thread)->tPriority == VM_THREAD_PRIORITY_NORMAL){
		NqueueR.push(tvectorT.at(thread));

	}
	else if(tvectorT.at(thread)->tPriority == VM_THREAD_PRIORITY_HIGH){
		HqueueR.push(tvectorT.at(thread));
	}

	if(tvectorT.at(thread)->tPriority > tvectorT.at(gID)->tPriority){
		tvectorT.at(gID)->tPriority = VM_THREAD_STATE_READY;
		if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_LOW){
			LqueueR.push(tvectorT.at(gID));
		}
		else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_NORMAL){
			NqueueR.push(tvectorT.at(gID));

		}
		else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_HIGH){
			HqueueR.push(tvectorT.at(gID));
		}
		Scheduler();
	}
	
	MachineResumeSignals(&SState);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadID(TVMThreadIDRef threadref){
	if(threadref == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}	
	else{
		(unsigned int) gID;
		*threadref = gID;
		return VM_STATUS_SUCCESS; 
	}
}

TVMStatus VMThreadTerminate(TVMThreadID thread){

	if(tvectorT.size() < thread){
		
		return VM_STATUS_ERROR_INVALID_ID;
	}
	else if(tvectorT.at(thread)->tState == VM_THREAD_STATE_DEAD){
		// << "STATEDEAD" << endl;
		
		return VM_STATUS_ERROR_INVALID_STATE;
	}
	else{
		tvectorT.at(thread)->tState = VM_THREAD_STATE_DEAD;
		if(tvectorT.at(thread)->tPriority == VM_THREAD_PRIORITY_HIGH){
		// << "terminate high" << endl;
			Thread * tempThread = new Thread;

			for(unsigned int i = 0; i < HqueueR.size(); i++)
			{
				tempThread = HqueueR.front();
				if(HqueueR.front()->tID == thread){
					if(HqueueR.front()->tState == VM_THREAD_STATE_RUNNING){
						HqueueR.front()->tState = VM_THREAD_STATE_DEAD;
						HqueueR.pop();
						Scheduler();
					}
					else{
						HqueueR.front()->tState = VM_THREAD_STATE_DEAD;
						HqueueR.pop();
					}
				}
				else{
					HqueueR.pop();
					HqueueR.push(tempThread);
				}
			}
		}
		else if(tvectorT.at(thread)->tPriority == VM_THREAD_PRIORITY_NORMAL){
			
			Thread * tempThread2 = new Thread;
			
			if(tvectorT.at(thread)->tState == VM_THREAD_STATE_READY){
				for(unsigned int j = 0; j < NqueueR.size(); j++){
					
					tempThread2 = HqueueR.front();
					if(NqueueR.front()->tID == thread){

						if(NqueueR.front()->tState == VM_THREAD_STATE_RUNNING){
							NqueueR.front()->tState = VM_THREAD_STATE_DEAD;
							NqueueR.pop();
							Scheduler();
						}
						else{
							NqueueR.front()->tState = VM_THREAD_STATE_DEAD;
							NqueueR.pop();
						}
					}
					else{
						NqueueR.pop();
						NqueueR.push(tempThread2);
					}

				}
			}

		}
		else if(tvectorT.at(thread)->tPriority == VM_THREAD_PRIORITY_LOW){
		// << "terminate low" << endl;
			Thread * tempThread3 = new Thread;

			for(unsigned int k = 0; k < NqueueR.size(); k++){
				tempThread3 = HqueueR.front();
				if(LqueueR.front()->tID == thread){
					if(LqueueR.front()->tState == VM_THREAD_STATE_RUNNING){
						LqueueR.front()->tState = VM_THREAD_STATE_DEAD;
						LqueueR.pop();
						Scheduler();
					}
					else{
						LqueueR.front()->tState = VM_THREAD_STATE_DEAD;
						LqueueR.pop();
					}
				}
				else{
					LqueueR.pop();
					LqueueR.push(tempThread3);
				}
			}
		}
		for(unsigned int i = 0; i < mVector.size(); i++){
			if(mVector.at(i)->id == (int) thread){
				mVector.at(i)->id = -1;
			}
		}
		Scheduler();
		return VM_STATUS_SUCCESS;
	}
}

TVMStatus VMThreadDelete(TVMThreadID thread){
	if(tvectorT.at(thread)->tState == VM_THREAD_STATE_DEAD){
		delete tvectorT.at(thread);
		tvectorT.at(thread) = NULL;
		return VM_STATUS_SUCCESS;
	}
	else if(thread > tvectorT.size()){
		return VM_STATUS_ERROR_INVALID_ID;
	}
	else{
		return VM_STATUS_ERROR_INVALID_STATE;
	}
}


TVMStatus VMMutexCreate(TVMMutexIDRef mutexref){
	
	TMachineSignalState SState;
	MachineSuspendSignals(&SState);

	mutex * mMutex = new mutex;
	*mutexref = mVector.size();
	mMutex->id = -1;
	mMutex->semaphore = 1;
	mMutex->mRef = mVector.size();
	mVector.push_back(mMutex);

	MachineResumeSignals(&SState);

	if(mutexref == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	else{
		return VM_STATUS_SUCCESS;
	}
}

TVMStatus VMMutexRelease(TVMMutexID mutex){
	
	TMachineSignalState SState;
	MachineSuspendSignals(&SState);
	if(mutex > mVector.size()){
		return VM_STATUS_ERROR_INVALID_ID;
	}
	else{
		if(mVector.at(mutex)->id == (int) gID){
		//<< "RELEASE " << mVector.at(mutex)->id  << "gID: " << gID << endl;
			mVector.at(mutex)->semaphore++;

			if(mVector.at(mutex)->semaphore <= 0){
				if(mVector.at(mutex)->mqueueH.empty() != true){
					mVector.at(mutex)->mqueueH.front()->tState = VM_THREAD_STATE_READY;
					HqueueR.push(mVector.at(mutex)->mqueueH.front());
					mVector.at(mutex)->id = mVector.at(mutex)->mqueueH.front()->tID;
					mVector.at(mutex)->mqueueH.pop();
				}
				else if(mVector.at(mutex)->mqueueN.empty() != true){
					mVector.at(mutex)->mqueueN.front()->tState = VM_THREAD_STATE_READY;
					NqueueR.push(mVector.at(mutex)->mqueueN.front());
					mVector.at(mutex)->id = mVector.at(mutex)->mqueueN.front()->tID;
					mVector.at(mutex)->mqueueN.pop();
				}
				else if(mVector.at(mutex)->mqueueL.empty() != true){
				//<< "low mutex" << mutex << endl;
					mVector.at(mutex)->mqueueL.front()->tState = VM_THREAD_STATE_READY;
					LqueueR.push(mVector.at(mutex)->mqueueL.front());
				//<< "old id: " << mVector.at(mutex)->id << " new id: " << mVector.at(mutex)->mqueueL.front()->tID << endl;
					mVector.at(mutex)->id = mVector.at(mutex)->mqueueL.front()->tID;
					mVector.at(mutex)->mqueueL.pop();
				}
				else {
					mVector.at(mutex)->id = -1;
					MachineResumeSignals(&SState);
					return VM_STATUS_SUCCESS;
				}
				if(tvectorT.at(mVector.at(mutex)->id)->tPriority > tvectorT.at(gID)->tPriority){
					tvectorT.at(gID)->tState = VM_THREAD_STATE_READY;
					if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_LOW){
						LqueueR.push(tvectorT.at(gID));
					}
					else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_NORMAL){
						NqueueR.push(tvectorT.at(gID));
					}
					else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_HIGH){
						HqueueR.push(tvectorT.at(gID));
					}
					Scheduler();
				}
			}
			else{
				MachineResumeSignals(&SState);
				return VM_STATUS_ERROR_INVALID_STATE;
			}
		}
		else{
			MachineResumeSignals(&SState);
			return VM_STATUS_ERROR_INVALID_STATE;
		}
	}
	MachineResumeSignals(&SState);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout){
	//<< "acquire mutex tid: " << mutex << endl;
	TMachineSignalState SState;
	MachineSuspendSignals(&SState);
	mVector.at(mutex)->semaphore = mVector.at(mutex)->semaphore - 1;
	// << "before if" << endl;
	if (mVector.at(mutex)->semaphore == 0){

		//<< "if " << gID << "mutex id: " << mVector.at(mutex)->id << endl;
		mVector.at(mutex)->id = (int) gID;
		MachineResumeSignals(&SState);
		return VM_STATUS_SUCCESS;
	}
	else if (timeout == VM_TIMEOUT_IMMEDIATE){
		//<< "im" << endl;
		MachineResumeSignals(&SState);
		return VM_STATUS_FAILURE;
	}
	else if (timeout == VM_TIMEOUT_INFINITE){
		//<< "semaphore: " << mVector.at(mutex)->semaphore << " current thread << " << gID << endl;
		if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_LOW){
			mVector.at(mutex)->mqueueL.push(tvectorT.at(gID));

		} 
		else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_NORMAL){
			mVector.at(mutex)->mqueueN.push(tvectorT.at(gID));
		}
		else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_HIGH){
			mVector.at(mutex)->mqueueH.push(tvectorT.at(gID));
		}
		tvectorT.at(gID)->tState = VM_THREAD_STATE_WAITING;
		Scheduler();
		MachineResumeSignals(&SState);
		return VM_STATUS_SUCCESS;
	}		
	else{

		VMThreadSleep(timeout);
		if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_LOW){
			mVector.at(mutex)->mqueueL.push(tvectorT.at(gID));
		}
		else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_NORMAL){
			mVector.at(mutex)->mqueueN.push(tvectorT.at(gID));
		}
		else if(tvectorT.at(gID)->tPriority == VM_THREAD_PRIORITY_HIGH){
			mVector.at(mutex)->mqueueH.push(tvectorT.at(gID));
		}
		MachineResumeSignals(&SState);
		return VM_STATUS_SUCCESS;
	}

}

TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){
	if(mutex > mVector.size()){
		return VM_STATUS_ERROR_INVALID_ID;
	}
	else if(ownerref == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	else{
		if(mVector.at(mutex)->id == -1){
			return mVector.at(mutex)->id = VM_THREAD_ID_INVALID;
		}
		else{
			*ownerref = mVector.at(mutex)->id;
			return VM_STATUS_SUCCESS; 
		}
	}
}

TVMStatus VMMutexDelete(TVMMutexID mutex){
	if(mVector.at(mutex)->id == -1){
		mVector.at(mutex) = NULL;
		return VM_STATUS_SUCCESS;
	}
	else if(mutex > mVector.size()){
		return VM_STATUS_ERROR_INVALID_ID;
	}
	else{
		return VM_STATUS_ERROR_INVALID_STATE;
	}
}
}
