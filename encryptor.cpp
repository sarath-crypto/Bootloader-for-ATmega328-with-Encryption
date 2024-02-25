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

#define SZ8		8
#define SZ16		16
#define PAGE_SZ		128

using namespace std;

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

unsigned char key[SZ16];
unsigned char kp = 0;
unsigned char bytes = 0;

void process_srec(string &srec){
	if(srec[0] == ':'){
		unsigned char ndata_bytes = hex2bin((unsigned char *)srec.c_str()+1);
		unsigned char type = hex2bin((unsigned char *)srec.c_str()+7);
		string data(srec.c_str()+9,ndata_bytes*2);
		for(int i = 0;i < data.length();i+=2){
			unsigned char b = hex2bin((unsigned char *)(data.c_str()+i));
			b ^= key[kp];
			kp++;
			if(kp >= SZ16)kp = 0;
			bytes++;
			if(bytes == 128){
				kp = 0;
				bytes = 0;
			}
			bin2hex(b,(unsigned char *)(data.c_str()+i));
		}
		memcpy((void  *)(srec.c_str()+9),data.c_str(),data.length());
		unsigned char cs  = 0;
		for(int i = 1;i < srec.length()-4;i+=2){
			unsigned char b = hex2bin((unsigned char *)(srec.c_str()+i));
			cs += b;
		}
		bin2hex((0x100-cs),(unsigned char *)(srec.c_str()+(srec.length()-4)));
	}
}

int main(int argc, char *argv[]){
	int r = 0;
	if(argc < 4){
		fprintf(stderr, "Usage ./encryptor <-r/-c> <file_in.hex> <file_out.hex> <key hex>\n");
		return 0;
	}
	string type = argv[1];
	string inf = argv[2];
	string outf = argv[3];
	string ukey = argv[4];

	ifstream in;
	ofstream out;
	in.open(inf, ios::in | ios ::binary);
        if(!in.is_open()){
                std::cerr << "Error: couldn't open " << inf << std::endl;
                return 0;
        }
	out.open(outf, ios::out | ios::binary);
        if(!out.is_open()){
                std::cerr << "Error: couldn't open " << outf << std::endl;
                return 0;
        }

	for(int i = 0,j = 0;i < SZ8*2;i+=2,j++)key[j] = hex2bin((unsigned char *)ukey.c_str()+i);
        for(unsigned char i = SZ8;i < SZ16;i++)key[i] = key[i-SZ8];
	if(!type.compare("-r")){
		key[8]  ^= 'S';
		key[9]  ^= 'E';
		key[10] ^= 'C';
		key[11] ^= 'R';
		key[12] ^= 'E';
		key[13] ^= 'T';
		key[14] ^= '0';
		key[15] ^= '1';
	}else{
		key[8]  ^= 'S';
		key[9]  ^= 'E';
		key[10] ^= 'C';
		key[11] ^= 'R';
		key[12] ^= 'E';
		key[13] ^= 'T';
		key[14] ^= '0';
		key[15] ^= '2';
	}
	string srec;
	while(true){
		unsigned char c = in.get();
		srec += c;
		if(c == '\n'){
			process_srec(srec);
			out << srec;
			srec.clear();
		}	
		if(in.eof())break;
	}
	out.close();
	in.close();
}
