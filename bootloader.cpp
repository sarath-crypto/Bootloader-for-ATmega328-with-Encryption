#define F_CPU   8000000UL

#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <avr/boot.h>
#include <avr/eeprom.h>

#define BLED		PB5

#define PDU_SZ  	8
#define UART_BAUD_SELECT(baudRate)  (((F_CPU)/ (16UL * baudRate))-1UL)

#define BAUD    	9600
#define PACKETS_PAGE    42
#define PAGE_SZ         128
#define TIMEOUT		32000
#define FL_KEY      	0x7FE8
#define SZ8		8
#define SZ16		16


volatile unsigned char  bpdu[PDU_SZ];
volatile unsigned char  bpp= 0;
volatile unsigned int   btmr = 0;
volatile unsigned char 	bpage[PAGE_SZ];
volatile unsigned char 	bp = 0;
volatile unsigned char  key[SZ16];	
volatile unsigned char  rkp = 0;
volatile unsigned char  boot_rdy = 0;

ISR (TIMER2_COMPA_vect){
        //2.08Khz ~0.48ms
        btmr++;
}

ISR(USART_RX_vect){
	bpdu[bpp] = UDR0;
        if ((bpdu[0] == '#') && (bpp < PDU_SZ))bpp++;
	else bpp = 0;
}

void init_io(void){
        cli();
        //Relocate interrupts to bootloader
        MCUCR = (1<<IVCE);
        MCUCR = (1<<IVSEL);

        //Init all io ports 
        //USART
        UCSR0B = _BV(TXEN0) | _BV(RXEN0) | _BV(RXCIE0);
        UCSR0C = _BV(UCSZ00) | _BV(UCSZ01);
        UBRR0L =  (unsigned char)UART_BAUD_SELECT(BAUD);
        UBRR0H =  (unsigned char)(UART_BAUD_SELECT(BAUD)>>8);

        //TIMER2 2.08khz
        OCR2A = 248;
        TCCR2A |= (1 << WGM21);
        TIMSK2 |= (1 << OCIE2A);
        TCCR2B |= (1 << CS21);

        sei();
}

void UART_bTxPDU(void){
        for(unsigned char p = 0;p < PDU_SZ;p++){
                while(!(UCSR0A & (1<<UDRE0)));
                UDR0 = bpdu[p];
        }
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

unsigned char read_eeprom_byte(unsigned int addr){
       unsigned char data =  eeprom_read_byte((unsigned char *)addr);
       eeprom_busy_wait();
       return data;
}

void  write_eeprom_byte(unsigned int addr,unsigned char data){
        eeprom_update_byte((unsigned char *)addr,data);
        eeprom_busy_wait();
}

void write_flash_page(unsigned int p){
        unsigned char sreg = SREG;
        cli();
    	eeprom_busy_wait ();
    	boot_page_erase (p);
    	boot_spm_busy_wait ();  
        for (unsigned char i = 0; i < PAGE_SZ; i+= 2){
                unsigned int data = (bpage[i+1] << 8) | bpage[i];
                boot_page_fill(p+i, data);
        }
        boot_page_write(p);
        boot_spm_busy_wait();
        boot_rww_enable();
        SREG = sreg;
	sei();
}

void read_flash_page(unsigned int p){
        for (unsigned char i = 0; i < PAGE_SZ; i+= 2){
                unsigned int data = pgm_read_word_near(p+i);
		bpage[i] = data;
		bpage[i+1] = data>>8;
        }
}

void send_page(void){
	bp = 0;
	unsigned char hx[2];
	
	for(unsigned char i = 0,kp = 0;i < PAGE_SZ;i++){
		bpage[i] ^= key[kp];
		kp++;
		if(kp >= SZ16)kp = 0;
	}
		
	for(unsigned char v = 0;v < PACKETS_PAGE;v++,bp+=3){
		bpdu[0] =  '#';
		bpdu[1] =  'c';
		bin2hex(bpage[bp],hx);
		bpdu[2] = hx[0];
		bpdu[3] = hx[1];
		bin2hex(bpage[bp+1],hx);
		bpdu[4] = hx[0];
		bpdu[5] = hx[1];
		bin2hex(bpage[bp+2],hx);
		bpdu[6] = hx[0];
		bpdu[7] = hx[1];
		UART_bTxPDU();
	}
	bpdu[0] =  '#';
	bpdu[1] =  'c';
	bin2hex(bpage[bp],hx);
	bpdu[2] = hx[0];
	bpdu[3] = hx[1];
	bin2hex(bpage[bp+1],hx);
	bpdu[4] = hx[0];
	bpdu[5] = hx[1];
	bin2hex(0,hx);
	bpdu[6] = hx[0];
	bpdu[7] = hx[1];
	UART_bTxPDU();
}

int main(void){
        DDRB = _BV(BLED);
	PORTB &= ~_BV(BLED);
	_delay_ms(500);
	if(eeprom_read_byte(0) == 0x00)asm("jmp 0000");

	init_io();	    
	bpp = 0;
	btmr = 0;
	bp = 0;
	for(unsigned char i = 0;i < SZ8;i++)key[i] = pgm_read_byte(FL_KEY+i);
	for(unsigned char i = SZ8;i < SZ16;i++)key[i] = key[i-SZ8];
	key[8]  ^= 'S';
	key[9]  ^= 'E';
	key[10] ^= 'C';
	key[11] ^= 'R';
	key[12] ^= 'E';
	key[13] ^= 'T';
	key[14] ^= '0';
	key[15] ^= '1';

     	unsigned char c = '0';

	while (1){
        	if(btmr >= TIMEOUT)asm("jmp 0000");
		if(boot_rdy == 0){
                        switch(btmr){
                                case(4000):
                                case(8000):
                                case(12000):
                                case(16000):
                                case(24000):
                                case(28000):{
                                                PORTB ^= _BV(BLED);
                                                bpdu[0] =  '#';
                                                bpdu[1] =  'B';
                                                bpdu[2] =  'O';
                                                bpdu[3] =  'O';
                                                bpdu[4] =  'T';
                                                bpdu[5] =  'L';
                                                bpdu[6] =  'D';
                                                bpdu[7] =  c;
                                                UART_bTxPDU();
                                                c++;
                                }
                        }
                }
		if (bpp >= PDU_SZ) {
			bpp = 0;
			if(bpdu[1] == 'a'){
				btmr = 0;
				bpage[bp++] = hex2bin((unsigned char *)&bpdu[2]);
				bpage[bp++] = hex2bin((unsigned char *)&bpdu[4]);
				if(bp < PAGE_SZ)bpage[bp++] = hex2bin((unsigned char *)&bpdu[6]);
				else bp = 0;
			}else if(bpdu[1] == 'b'){
				unsigned char bnp  = hex2bin((unsigned char *)&bpdu[2]);
				
				for(unsigned char i = 0,kp = 0;i < PAGE_SZ;i++){
					bpage[i] ^= key[kp];
					kp++;
					if(kp >= SZ16)kp = 0;
				}
					
				write_flash_page(bnp*PAGE_SZ);
				_delay_ms(10);
				bp = 0;
			}else if(bpdu[1] == 'c'){
				unsigned char bnp  = hex2bin((unsigned char *)&bpdu[2]);
				read_flash_page(bnp*PAGE_SZ);
				send_page();
				bp = 0;
				PORTB ^= _BV(BLED);
			}else if(bpdu[1] == 'd'){
				unsigned int addr = hex2bin((unsigned char *)&bpdu[2]) << 8| hex2bin((unsigned char *)&bpdu[4]);
				unsigned char data = hex2bin((unsigned char *)&bpdu[6]);
				write_eeprom_byte(addr,data);
			}else if(bpdu[1] == 'e'){
				unsigned int addr = hex2bin((unsigned char *)&bpdu[2]) << 8| hex2bin((unsigned char *)&bpdu[4]);
				unsigned char hx[2];
				bpdu[0] =  '#';
				bpdu[1] =  'e';
				bin2hex(read_eeprom_byte(addr),hx);
				bpdu[2] = hx[0];
				bpdu[3] = hx[1];
				bin2hex(read_eeprom_byte(addr+1),hx);
				bpdu[4] = hx[0];
				bpdu[5] = hx[1];
				bin2hex(read_eeprom_byte(addr+2),hx);
				bpdu[6] = hx[0];
				bpdu[7] = hx[1];
				UART_bTxPDU();
			}
		}
	}
}
