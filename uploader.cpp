#include <iostream>
#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <iomanip>
#include <algorithm>
#include <string>
#include <vector>
#include <bitset>
#include <pthread.h>
#include <cstdlib>
#include <fstream>
#include <bits/stdc++.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <cstring>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include "rs232.h"

#define PDU_SZ          8
#define PAYLOAD_SZ	3
#define PACKETS_PAGE	42
#define SZ8		8

#define KEY_SZ          16
#define PAGE_SZ         128
#define BOOT_ADDR       0x7000
#define PAGE_MAX        256
#define PAGE_SEC        0x6F80

#define PORT		"ttyUSB0"

using namespace std;

typedef struct mem{
	unsigned char data;
	unsigned short addr;
}mem;

unsigned char rnp;
unsigned char rpage[PAGE_MAX][PAGE_SZ];
unsigned char tpage[PAGE_SZ];
vector<unsigned char>rxb;
vector<unsigned char>txb;
vector<mem>wm;

int nPorts;
int pIndex;

enum state{MEM = 1,WAIT,FLASH};

bool boot_rdy = false;
bool ex = false;
bool hold = false;
unsigned char state = 0;
pthread_t rx_thread;

void sig_handler(int signo){
	ex = true;
}

void quit(void){
	comClose(pIndex);
	pthread_join(rx_thread,NULL);
}
void bin2hex(unsigned char bin, unsigned char *phex) {
        const char h[] = "0123456789ABCDEF";
        unsigned char nb = bin >> 4;
        *phex = h[nb];
        nb = bin & 0x0F;
        *(phex + 1) = h[nb];
}

int hex2int(char c) {
        int first = c / 16 - 3;
        int second = c % 16;
        int result = first * 10 + second;
        if (result > 9) result--;
        return result;
}

unsigned char hex2bin(unsigned char *phex) {
        int high = hex2int(*phex) * 16;
        int low = hex2int(*(phex + 1));
        return (high + low);
}

int kbhit(void) {
        static bool initflag = false;
        static const int STDIN = 0;
        if (!initflag){
                struct termios term;
                tcgetattr(STDIN, &term);
                term.c_lflag &= ~ICANON;
                tcsetattr(STDIN, TCSANOW, &term);
                setbuf(stdin, NULL);
                initflag = true;
        }
        int nbbytes;
        ioctl(STDIN, FIONREAD, &nbbytes);
        return nbbytes;
}

void read_eeprom(unsigned short addr){
	int sbytes = 0;
	unsigned char hx[2];
	txb.clear();
	txb.push_back('#');
	txb.push_back('e');
	bin2hex(addr>>8,hx);
	txb.push_back(hx[0]);
	txb.push_back(hx[1]);
	bin2hex(addr,hx);
	txb.push_back(hx[0]);
	txb.push_back(hx[1]);
	bin2hex(0,hx);
	txb.push_back(hx[0]);
	txb.push_back(hx[1]);
	sbytes =  comWrite(pIndex,(char *)txb.data(),txb.size());
	if(sbytes != txb.size()){
		comClose(pIndex);
		fprintf(stderr,"\ncomWrite error");
		ex =  true;
	}
	hold = true;
}

void write_eeprom(unsigned short addr,unsigned char data){
	int sbytes = 0;
	unsigned char hx[2];
	txb.clear();
	txb.push_back('#');
	txb.push_back('d');
	bin2hex(addr>>8,hx);
	txb.push_back(hx[0]);
	txb.push_back(hx[1]);
	bin2hex(addr,hx);
	txb.push_back(hx[0]);
	txb.push_back(hx[1]);
	bin2hex(data,hx);
	txb.push_back(hx[0]);
	txb.push_back(hx[1]);
	sbytes =  comWrite(pIndex,(char *)txb.data(),txb.size());
	if(sbytes != txb.size()){
		comClose(pIndex);
		fprintf(stderr,"\ncomWrite error");
		ex =  true;
	}
}

void process_srec(string &srec){
	if(srec[0] == ':'){
		unsigned char ndata = hex2bin((unsigned char *)srec.c_str()+1);
		unsigned short addr =  hex2bin((unsigned char *)srec.c_str()+3);
		addr = addr << 8;
		addr |=  hex2bin((unsigned char *)srec.c_str()+5);
		unsigned char type = hex2bin((unsigned char *)srec.c_str()+7);
		string data(srec.c_str()+9,ndata*2);
		for(int i = 0;i < data.length();i+=2){
			unsigned char b = hex2bin((unsigned char *)(data.c_str()+i));
		}
		unsigned char cs  = 0;
		for(int i = 1;i < srec.length()-4;i+=2){
			unsigned char b = hex2bin((unsigned char *)(srec.c_str()+i));
			cs += b;
		}
		cs = 0x100-cs;
		if(cs != hex2bin((unsigned char *)srec.c_str()+srec.length()-4)){
			cout << "Invalid checksum"<<endl;
			ex =  true;
		}
		mem m;
		m.addr = addr;
 		for(int i = 0;i < data.length();i+=2){
                        m.data = hex2bin((unsigned char *)(data.c_str()+i));
			m.addr = addr++;
			wm.push_back(m);	
                }
	}
}


int getposyx(int *y, int *x) {
	char buf[30]={0};
	int ret, i, pow;
	char ch;
	*y = 0; *x = 0;
	struct termios term, restore;

	tcgetattr(0, &term);
	tcgetattr(0, &restore);
	term.c_lflag &= ~(ICANON|ECHO);
	tcsetattr(0, TCSANOW, &term);

	write(1, "\033[6n", 4);
	for( i = 0, ch = 0; ch != 'R'; i++ ){
		ret = read(0, &ch, 1);
		if ( !ret ) {
			tcsetattr(0, TCSANOW, &restore);
			fprintf(stderr, "getpos: error reading response!\n");
			return 1;
		}
		buf[i] = ch;
	}
	if (i < 2) {
		tcsetattr(0, TCSANOW, &restore);
		printf("i < 2\n");
		return(1);
	}
	for( i -= 2, pow = 1; buf[i] != ';'; i--, pow *= 10)
		*x = *x + ( buf[i] - '0' ) * pow;
	for( i-- , pow = 1; buf[i] != '['; i--, pow *= 10)
		*y = *y + ( buf[i] - '0' ) * pow;
	tcsetattr(0, TCSANOW, &restore);
	return 0;
}
void *recv_thread(void *ptr){
	int r = 0;
	char rx = 0;
	vector <unsigned char > page;
	unsigned char hx[2];
	int sbytes = 0;
	unsigned char n  = 0;
	unsigned char pc = 0;
	while(!ex){
		r = comRead(pIndex,(char *)&rx,1);	
		if(r > 0)rxb.push_back(rx);	
		if((rxb.size() && (rxb[0] != '#')))rxb.clear();
		if(rxb.size() >= PDU_SZ) {
			unsigned char *phex = rxb.data();
			switch(rxb[1]){
				case('B'):{

					if(boot_rdy)break;
					string msg(rxb.begin(),rxb.end());
					string cmsg(msg.begin(),msg.end()-1);
					if(cmsg.compare("#BOOTLD") == 0){
						cout <<"Recieved message:"<< msg << endl;
						boot_rdy = true;
					}
					break;
				}
				case('c'):{
					for(int i = 0,j = 2;i < PDU_SZ-2;i+=2,j++)rxb[j] =  hex2bin(phex+2+i);
                                	tpage[n++] = rxb[2];
                                	tpage[n++] = rxb[3];
                                	if(n == PAGE_SZ){
						if(memcmp(tpage,rpage[pc],PAGE_SZ)){
							fprintf(stderr,"Verification failed for flash page: %d\n",pc);
							ex = true;
							quit();
						}
						n = 0;
						pc++;
						hold = false;
                                	}else tpage[n++] = rxb[4];
					break;
                        	}
				case('e'):{
				        fprintf(stderr,"done\n");
					hold = false;
					break;
				}
				default:{
					fprintf(stderr,"Unknown PDU :%c %02x%02x%02x\n",rxb[1],rxb[2],rxb[3],rxb[4]);
				}
			}
			rxb.clear();
		}
		if(ex)hold = false;
	}
	return NULL;
}

void printyx(int x, int y, const char *format, ...){
	va_list args;
	va_start(args, format);
	printf("\033[%d;%dH", x, y);
	vprintf(format, args);
	va_end(args);
	fflush(stdout);
}

void my_handler(int s){
	ex = true;
}

int main(int argc, char *argv[]){
	int r = 0;
	if(argc < 2){
		fprintf(stderr, "Usage ./uploader <file_in.hex>\n");
		return 0;
	}
	string inf = argv[1];
	ifstream in;
	in.open(inf, ios::in | ios ::binary);
        if(!in.is_open()){
                std::cerr << "Error: couldn't open " << inf << std::endl;
                return 0;
        }

	struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = my_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);

	string srec;
	while(true){
		unsigned char c = in.get();
		srec += c;
		if(c == '\n'){
			process_srec(srec);
			srec.clear();
		}	
		if(in.eof())break;
		if(ex)break;
	}
	in.close();

	unsigned char data = 0;
        unsigned int nb = 0;
        for(int i = 0;i < wm.size();i++,nb++){
                rpage[rnp][nb] = wm[i].data;
                if(nb == PAGE_SZ){
                        nb = 0;
                        rnp++;
                }
        }
        if(nb != PAGE_SZ){
                for(nb;nb != PAGE_SZ;nb++)rpage[rnp][nb] = 0xff;
                rnp++;
        }
	wm.clear();
  	
	if (signal(SIGINT, sig_handler) == SIG_ERR)fprintf(stderr,"Error SIGINT\n");
	nPorts = comEnumerate();
        pIndex = comFindPort(PORT);
        if(pIndex < 0){
		fprintf(stderr,"Com port not available %s\n",PORT);
		return 0;
	}
        if(comOpen(pIndex,9600) < 0){
		fprintf(stderr,"Com port open failed %s\n",PORT);
		return 0;
	}
	if(pthread_create(&rx_thread, NULL,recv_thread,(void *)nullptr)){
		fprintf(stderr, "Error creating thread\n");
		comClose(pIndex);
		return 0;
	}
	while(!ex)if(boot_rdy)break;
	hold = true;	
	while(hold != false){
		fprintf(stderr,"\nReading configuration...");
		read_eeprom(0);
		if(ex){
			hold =  false;
			quit();
		}
		sleep(1);
	}
	unsigned char hx[2];
	int x = 0, y = 0;
	fprintf(stderr,"Completed : [          ]");
	getposyx(&y,&x);
	int adv = 0;
	int adv_prev = 0;
	int xa = 1;
	printyx(y,x-12+xa,"#");
	unsigned char f = 0;
	while(!ex){
		nb = 0;
		for(unsigned char np = 0;np < PACKETS_PAGE;np++){
			txb.clear();
			txb.push_back('#');
			txb.push_back('a');
			for(unsigned char l = 0;l < PAYLOAD_SZ;l++,nb++){
				data = rpage[f][nb];
				bin2hex(data,hx);
				txb.push_back(hx[0]);
				txb.push_back(hx[1]);
			}
			if(ex)break;
			r =  comWrite(pIndex,(char *)txb.data(),txb.size());
			if(r != txb.size()){
				fprintf(stderr,"\ncomWrite error");
				ex =  true;
			}
		}
		txb.clear();
		txb.push_back('#');
		txb.push_back('a');
		data = rpage[f][nb];
		nb++;
		bin2hex(data,hx);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		data = rpage[f][nb];
		bin2hex(data,hx);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		bin2hex(0,hx);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		r =  comWrite(pIndex,(char *)txb.data(),txb.size());
		if(r != txb.size()){
			fprintf(stderr,"\ncomWrite error");
			ex =  true;
		}
		txb.clear();
		txb.push_back('#');
		txb.push_back('b');
		bin2hex(f,hx);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		r =  comWrite(pIndex,(char *)txb.data(),txb.size());
		if(r != txb.size()){
			fprintf(stderr,"\ncomWrite error");
			ex =  true;
		}
		sleep(1);
		txb.clear();
		txb.push_back('#');
		txb.push_back('c');
		bin2hex(f,hx);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		txb.push_back(hx[0]);
		txb.push_back(hx[1]);
		r =  comWrite(pIndex,(char *)txb.data(),txb.size());
		if(r != txb.size()){
			fprintf(stderr,"\ncomWrite error");
			ex =  true;
		}
		hold = true;
		while(hold != false){
			if(ex)break;
		}
		adv = (int)(((float)f/(float)rnp)*100)/10;
		if(adv_prev != adv){
			adv_prev = adv;
			xa++;
			printyx(y,x-12+xa,"#");
		}
		f++;
		if(f == rnp)ex = true;
	}
	fprintf(stderr,"\n");
	ex = true;
	quit();
}
