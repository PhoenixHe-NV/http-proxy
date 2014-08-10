#ifndef _PROXY_MAIN_H_
#define _PROXY_MAIN_H_

enum main_stat_e {
    RUNING = 0, 
    EXITING = 1
};

extern enum main_stat_e main_stat;

int proxy_main(int argc, char** argv);

#endif
