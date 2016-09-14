#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <vector>
#include <cstring>
#include <iostream>
#include <fstream>

struct termios SavedTermAttributes;
char RXChar;
int count = 0;
int historycount = 0;
std::vector<char> listchar; 
std::vector<std::vector<char> > hist;
int newhist = 0;
bool alreadyRead = false;

void ResetCanonicalMode(int fd, struct termios *savedattributes){
    tcsetattr(fd, TCSANOW, savedattributes);
}

void SetNonCanonicalMode(int fd, struct termios *savedattributes){
    struct termios TermAttributes;
    char *name;
    
    // Make sure stdin is a terminal. 
    if(!isatty(fd)){
        fprintf (stderr, "Not a terminal.\n");
        exit(0);
    }
    
    // Save the terminal attributes so we can restore them later. 
    tcgetattr(fd, savedattributes);
    
    // Set the funny terminal modes. 
    tcgetattr (fd, &TermAttributes);
    TermAttributes.c_lflag &= ~(ICANON | ECHO); // Clear ICANON and ECHO. 
    TermAttributes.c_cc[VMIN] = 1;
    TermAttributes.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &TermAttributes);
}

void displayChar(){
    count++;
    listchar.push_back(RXChar);
    write(1, &RXChar, 1);
}

void enter(){
            newhist = 0;
            hist.push_back(listchar);
            listchar.clear();
            historycount++;

            write(1, &RXChar, 1);
            alreadyRead = false;
            count = 0;
}

int main(int argc, char *argv[]){

    SetNonCanonicalMode(STDIN_FILENO, &SavedTermAttributes);

    while(1){
        if(!alreadyRead) {
            read(STDIN_FILENO, &RXChar, 1);
        }
        if(0x04 == RXChar){ // C-d
            break;
        }
        // exit
        if(RXChar == 0x65){ // e

            displayChar();
            read(STDIN_FILENO, &RXChar, 1);
            alreadyRead = true;
            if(RXChar == 0x78){ // x
                displayChar();
                read(STDIN_FILENO, &RXChar, 1);
                alreadyRead = true;
                if(RXChar == 0x69){ // i
                    displayChar();
                    read(STDIN_FILENO, &RXChar, 1);
                    alreadyRead = true;
                    if(RXChar == 0x74){ // t
                        displayChar();
                        read(STDIN_FILENO, &RXChar, 1);
                        alreadyRead = true;
                        if(RXChar == 0x0A){
                            write(1, &RXChar, 1);
                            break;
                        }
                    }
                }
            }
        }
        if(RXChar == 0x1B){ 
            read(STDIN_FILENO, &RXChar, 1);
            if(RXChar == 0x5B){
                read(STDIN_FILENO, &RXChar, 1);

                if(RXChar == 0x41){ // up arrow

                    newhist++;
                    if(historycount-newhist < 0)
                    {
                        write(1, "\a", 1);
                        newhist--;
                    }
                    else{
                        for(int i = 0; i < count; i++){
                            write(1, "\b \b", 3); // delete current line
                        }
                        for(int i = 0; i < hist.at(historycount-newhist).size(); i++){
                            write(1, &hist.at(historycount-newhist).at(i), 1); // write history
                        }
                        count = hist.at(historycount-newhist).size();
                        listchar.clear();
                        for(int i = 0; i<hist.at(historycount-newhist).size(); i++){
                            listchar.push_back(hist.at(historycount-newhist).at(i)); // push written history onto list
                        }
                    }
                }
                if(RXChar == 0x42){ // down arrow
                    newhist--;
                    if(newhist - 1 < 0){
                        write(1, "\a", 1);
                        newhist++;
                    }
                    else{
                        for(int i = 0; i < count; i++){
                                write(1, "\b \b", 3); // delete current line
                        }
                        for(int i = 0; i < hist.at(historycount-newhist).size(); i++){
                            write(1, &hist.at(historycount-newhist).at(i), 1); // write history
                        }
                        count = hist.at(historycount-newhist).size();
                        listchar.clear();
                        for(int i = 0; i<hist.at(historycount-newhist).size(); i++){
                            listchar.push_back(hist.at(historycount-newhist).at(i)); // push written history onto list
                        }
                    }
                }
            }
        }

        else if(RXChar == 0x70){ // p
            displayChar();
            read(STDIN_FILENO, &RXChar, 1);
            
            if(RXChar == 0x77){ // w
                displayChar();
                read(STDIN_FILENO, &RXChar, 1);
                if(RXChar == 0x64){ // d
                    displayChar();
                    read(STDIN_FILENO, &RXChar, 1);
                    if(0x0A == RXChar){
                        enter();

                        write(1, get_current_dir_name(), strlen(get_current_dir_name()));

                        write(1, "\n", 1);
                    }
                }
            }
        }
        else if(RXChar == 0x7F){ // delete
            if(count == 0) {
                write(1, "\a", 1);  // ring bell if line is empty
            }
            else {
                listchar.pop_back();
                write(1, &RXChar, 1); // delete a character and remove from history
                count--;
            }
            alreadyRead = false;
        }
        else if(0x1B == RXChar){ // esc

        }
        else if(0x0A == RXChar){ // enter
            enter();
        } 
        else if(isprint(RXChar)) {
            displayChar();
            alreadyRead = false;
        }
    }
    ResetCanonicalMode(STDIN_FILENO, &SavedTermAttributes);
    return 0;
}

