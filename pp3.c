#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>


#if defined(__linux__) || defined(__APPLE__)
#include <termios.h>
#else
#include <windows.h>
#endif

int setCPUtype(char* cpu);
int parse_hex (char * filename, unsigned char * progmem, unsigned char * config);
size_t getlinex(char **lineptr, size_t *n, FILE *stream) ;
void comErr(char *fmt, ...);
void flsprintf(FILE* f, char *fmt, ...);

char * COM = "";
char * PP_VERSION3 = "0.99";

#define	PROGMEM_LEN	260000
#define	CONFIG_LEN	35
#define	CF_P18F_Q43	12
#define	CF_P18F_Q8x	13

int verbose = 1,verify = 1,program = 1,sleep_time = 0;
int devid_expected,devid_mask,baudRate,com,flash_size,page_size,chip_family,config_size;
unsigned char file_image[70000],progmem[PROGMEM_LEN], config_bytes[CONFIG_LEN];

//*********************************************************************************//
//*********************************************************************************//
//*********************************************************************************//
//               serial IO interfaces for Linux and windows
//*********************************************************************************//
//*********************************************************************************//
//*********************************************************************************//

#if defined(__linux__) || defined(__APPLE__)

void initSerialPort()
    {
    baudRate=B57600;
    if (verbose>2)
        printf("Opening: %s at %d\n",COM,baudRate);
    com =  open(COM, O_RDWR | O_NOCTTY | O_NDELAY);
    if (com <0) comErr("Failed to open serial port");

    struct termios opts;
    memset (&opts,0,sizeof (opts));

    fcntl(com, F_SETFL, 0);
    if (tcgetattr(com, &opts)!=0) printf("Err tcgetattr\n");

    cfsetispeed(&opts, baudRate);
    cfsetospeed(&opts, baudRate);
    opts.c_lflag  &=  ~(ICANON | ECHO | ECHOE | ISIG);

    opts.c_cflag |=  (CLOCAL | CREAD);
    opts.c_cflag &=  ~PARENB;
    opts.c_cflag &= ~CSTOPB;
    opts.c_cflag &=  ~CSIZE;
    opts.c_cflag |=  CS8;
    opts.c_oflag &=  ~OPOST;
    opts.c_iflag &=  ~INPCK;
    opts.c_iflag &=  ~ICRNL;		//do NOT translate CR to NL
    opts.c_iflag &=  ~(IXON | IXOFF | IXANY);
    opts.c_cc[ VMIN ] = 0;
    opts.c_cc[ VTIME ] = 10;//0.1 sec
    if (tcsetattr(com, TCSANOW, &opts) != 0)
        {
        perror(COM);
        printf("set attr error");
        abort();
        }
    tcflush(com,TCIOFLUSH); // just in case some crap is the buffers
    }


void putByte(int byte)
    {
    char buf = byte;
    if (verbose>3) flsprintf(stdout,"TX: 0x%02X\n", byte);
    int n = write(com, &buf, 1);
    if (n != 1) comErr("Serial port failed to send a byte, write returned %d\n", n);
    }


void putBytes (unsigned char * data, int len)
    {

    int i;
    for (i=0; i<len; i++)
        putByte(data[i]);
    /*
    if (verbose>3)
        flsprintf(stdout,"TXP: %d B\n", len);
    int n = write(com, data, len);
    if (n != len)
        comErr("Serial port failed to send %d bytes, write returned %d\n", len,n);
    */
    }

int getByte()
    {
    char buf;
    int n = read(com, &buf, 1);
    if (verbose>3) flsprintf(stdout,n<1?"RX: fail\n":"RX:  0x%02X\n", buf & 0xFF);
    if (n == 1) return buf & 0xFF;

    comErr("Serial port failed to receive a byte, read returned %d\n", n);
    return -1; // never reached
    }
#else

HANDLE port_handle;

void initSerialPort()
    {

    char mode[40],portname[20];
    COMMTIMEOUTS timeout_sets;
    DCB port_sets;
    strcpy(portname,"\\\\.\\");
    strcat(portname,COM);
    port_handle = CreateFileA(portname,
                                GENERIC_READ|GENERIC_WRITE,
                                0,                          /* no share  */
                                NULL,                       /* no security */
                                OPEN_EXISTING,
                                0,                          /* no threads */
                                NULL);                      /* no templates */
    if(port_handle==INVALID_HANDLE_VALUE)
        {
        printf("unable to open port %s -> %s\n",COM, portname);
        exit(0);
        }
    strcpy (mode,"baud=57600 data=8 parity=n stop=1");
    memset(&port_sets, 0, sizeof(port_sets));  /* clear the new struct  */
    port_sets.DCBlength = sizeof(port_sets);

    if(!BuildCommDCBA(mode, &port_sets))
        {
        printf("dcb settings failed\n");
        CloseHandle(port_handle);
        exit(0);
        }

    if(!SetCommState(port_handle, &port_sets))
        {
        printf("cfg settings failed\n");
        CloseHandle(port_handle);
        exit(0);
        }

    timeout_sets.ReadIntervalTimeout         = 1;
    timeout_sets.ReadTotalTimeoutMultiplier  = 1000;
    timeout_sets.ReadTotalTimeoutConstant    = 1;
    timeout_sets.WriteTotalTimeoutMultiplier = 1000;
    timeout_sets.WriteTotalTimeoutConstant   = 1;

    if(!SetCommTimeouts(port_handle, &timeout_sets))
        {
        printf("timeout settings failed\n");
        CloseHandle(port_handle);
        exit(0);
        }


    }
void putByte(int byte)
    {
    int n;
    if (verbose>3) flsprintf(stdout,"TX: 0x%02X\n", byte);
    WriteFile(port_handle, &byte, 1, (LPDWORD)((void *)&n), NULL);
    if (n != 1) comErr("Serial port failed to send a byte, write returned %d\n", n);
    }

void putBytes (unsigned char * data, int len)
    {
    /*
    int i;
    for (i=0;i<len;i++)
    putByte(data[i]);
    */
    int n;
    WriteFile(port_handle, data, len, (LPDWORD)((void *)&n), NULL);
    if (n != len) comErr("Serial port failed to send a byte, write returned %d\n", n);
    }



int getByte()
    {
    unsigned char buf[2];
    int n;
    ReadFile(port_handle, buf, 1, (LPDWORD)((void *)&n), NULL);
    if (verbose>3) flsprintf(stdout,n<1?"RX: fail\n":"RX:  0x%02X\n", buf[0] & 0xFF);
    if (n == 1) return buf[0] & 0xFF;
    comErr("Serial port failed to receive a byte, read returned %d\n", n);
    return -1; // never reached
    }
#endif


//*********************************************************************************//
//*********************************************************************************//
//*********************************************************************************//
//               generic routines
//*********************************************************************************//
//*********************************************************************************//
//*********************************************************************************//

void comErr(char *fmt, ...)
    {
    char buf[ 500 ];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    fprintf(stderr,"%s", buf);
    perror(COM);
    va_end(va);
    abort();
    }

void flsprintf(FILE* f, char *fmt, ...)
    {
    char buf[ 500 ];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    fprintf(f,"%s", buf);
    fflush(f);
    va_end(va);
    }

int is_empty (unsigned char * buff, int len)
    {
    int i,empty;
    empty = 1;
    for (i=0; i<len; i++)
        if (buff[i]!=0xFF) empty = 0;
    return empty;
    }

//get line replacement
size_t getlinex(char **lineptr, size_t *n, FILE *stream)
    {
    char *bufptr = NULL;
    char *p = bufptr;
    size_t size;
    int c;

    if (lineptr == NULL)return -1;
    if (stream == NULL) return -1;
    if (n == NULL) return -1;
    bufptr = *lineptr;
    size = *n;

    c = fgetc(stream);
    if (c == EOF) return -1;
    if (bufptr == NULL)
        {
        bufptr = malloc(128);
        if (bufptr == NULL)
            {
            return -1;
            }
        size = 128;
        }
    p = bufptr;
    while(c != EOF)
        {
        if ((p - bufptr) > (size - 1))
            {
            size = size + 128;
            bufptr = realloc(bufptr, size);
            if (bufptr == NULL)
                {
                return -1;
                }
            }
        *p++ = c;
        if (c == '\n')
            {
            break;
            }
        c = fgetc(stream);
        }
    *p++ = '\0';
    *lineptr = bufptr;
    *n = size;
    return p - bufptr - 1;
    }

void sleep_ms (int num)
    {
    struct timespec tspec;
    tspec.tv_sec=num/1000;
    tspec.tv_nsec=(num%1000)*1000000;
    nanosleep(&tspec,0);
    }

void printHelp()
    {
    flsprintf(stdout,"pp programmer\n");
    exit(0);
    }


void parseArgs(int argc, char *argv[])
    {
    int c;
    while ((c = getopt (argc, argv, "c:nps:t:v:")) != -1)
        {
        switch (c)
            {
            case 'c' :
                COM=optarg;
                break;
            case 'n':
                verify = 0;
                break;
            case 'p':
                program = 0;
                break;
            case 's' :
                sscanf(optarg,"%d",&sleep_time);
                break;
            case 't' :
                setCPUtype(optarg);
                break;
            case 'v' :
                sscanf(optarg,"%d",&verbose);
                break;
            case '?' :
                if (isprint (optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr,"Unknown option character `\\x%x'.\n",optopt);
            default:
                fprintf (stderr,"Bug, unhandled option '%c'\n",c);
                abort ();
            }
        }
    if (argc<=1)
        printHelp();
    }

//*********************************************************************************//
//*********************************************************************************//
//*********************************************************************************//
//               programming routines
//*********************************************************************************//
//*********************************************************************************//
//*********************************************************************************//

int setCPUtype(char* cpu)
    {
    int name_len,i,read;
    name_len = strlen (cpu);
    for(i = 0; i<name_len; i++) cpu[i] = tolower(cpu[i]);
    char * line = NULL;
    char * filename = "pp3_devices.dat";
    char read_cpu_type[20], read_algo_type[20];
    int read_flash_size, read_page_size, read_id, read_mask;

    size_t len = 0;
    if (verbose>2) printf ("Opening filename %s \n", filename);
    FILE* sf = fopen(filename, "r");
    if (sf==0)
        {
        return -1;
        if (verbose>0) printf ("Can't open database file %s\n",filename);
        }
    if (verbose>2) printf ("File open\n");
    while ((read =  getlinex(&line, &len, sf)) != -1)
        {
        if (verbose>3) printf("\nRead %d chars: %s",read,line);
        if (line[0]!='#')
			{
			sscanf (line,"%s %d %d %x %x %s",(char*)&read_cpu_type,&read_flash_size,&read_page_size,&read_id,&read_mask,(char*)&read_algo_type);
			if (verbose>3) printf("\n*** %s,%d,%d,%x,%x,%s",read_cpu_type,read_flash_size,read_page_size,read_id,read_mask,read_algo_type);
			if (strcmp(read_cpu_type,cpu)==0)
				{
				flash_size = read_flash_size;
				page_size = read_page_size;
				devid_expected = read_id;
				devid_mask = read_mask;
				if (verbose>1) printf("Found database match %s,%d,%d,%x,%x,%s\n",read_cpu_type,read_flash_size,read_page_size,read_id,read_mask,read_algo_type);
				if (strcmp("CF_P18F_Q43",read_algo_type)==0) chip_family = CF_P18F_Q43;
				if (strcmp("CF_P18F_Q8x",read_algo_type)==0) chip_family = CF_P18F_Q8x;
				if (chip_family == CF_P18F_Q43) config_size = 10;
				if (chip_family == CF_P18F_Q8x) config_size = 35;
				if (verbose>2) printf("chip family:%d, config size:%d\n",chip_family,config_size);
				}
			}
        }
    fclose(sf);
    return 0;
    }


int p16c_mass_erase (void)
    {
    if (verbose>2) flsprintf(stdout,"Mass erase\n");
    putByte(0x43);
    putByte(0x00);
    getByte();
    return 0;
    }

int p16c_read_page (unsigned char * data, int address, unsigned char num)
    {
    unsigned char i;
    address = address / 2;
    if (verbose>2) flsprintf(stdout,"Reading page of %d bytes at 0x%6.6x\n", num, address);
    putByte(0x41);
    putByte(0x04);
    putByte(num/2);
    putByte((address>>16)&0xFF);
    putByte((address>>8)&0xFF);
    putByte((address>>0)&0xFF);
    getByte();
    for (i=0; i<num; i++)
        {
        *data++ = getByte();
        }
//    for (i=0; i<num; i++) if (verbose>2) flsprintf(stdout,"%2.2x ", data[i]);

    return 0;
    }

int p18q_write_single_cfg (unsigned char data1, int address)
    {
    if (verbose>2) flsprintf(stdout,"Writing cfg 0x%2.2x at 0x%6.6x\n", data1, address);
    putByte(0x45);
    putByte(5);
    putByte(0);
    putByte((address>>16)&0xFF);
    putByte((address>>8)&0xFF);
    putByte((address>>0)&0xFF);
    putByte(data1);
    getByte();
    return 0;
    }

int p18q_write_page (unsigned char * data, int address, unsigned char num)
    {
    unsigned char i, empty;
    address = address / 2;
    empty = 1;
    for (i=0; i<num; i=i+2)
		{
		if 	((data[i]!=0xFF)|(data[i+1]!=0xFF))
			empty = 0;
		}
    if (verbose>2) flsprintf(stdout,"Writing A page of %d bytes at 0x%6.6x\n", num, address);
    if (empty==1)
        {
        if (verbose>3)
            flsprintf(stdout,"~");
        return 0;
        }
    putByte(0x46);
    putByte(4+num);
    putByte(num);
    putByte((address>>16)&0xFF);
    putByte((address>>8)&0xFF);
    putByte((address>>0)&0xFF);
    for (i=0; i<num; i++)
        putByte(data[i]);
    getByte();
    return 0;
    }

int p18q_read_cfg (unsigned char * data, int address, unsigned char num)
    {
    unsigned char i;
    if (verbose>2) flsprintf(stdout,"Reading config of %d bytes at 0x%6.6x\n", num, address);
    putByte(0x47);
    putByte(0x04);
    putByte(num);
    putByte((address>>16)&0xFF);
    putByte((address>>8)&0xFF);
    putByte((address>>0)&0xFF);
    getByte();
    for (i=0; i<num; i++)
        {
        *data++ = getByte();
        }
//    for (i=0; i<num; i++) if (verbose>2) flsprintf(stdout,"%2.2x ", data[i]);

    return 0;
    }

int prog_enter_progmode (void)
    {
    if (verbose>2) flsprintf(stdout,"Entering programming mode\n");
    putByte(0x40);
    putByte(0x00);
    getByte();
    return 0;
    }

int prog_exit_progmode (void)
    {
    if (verbose>2) flsprintf(stdout,"Exiting programming mode\n");
    putByte(0x02);
    putByte(0x00);
    getByte();
    return 0;
    }

int prog_get_device_id (void)
    {
    unsigned char mem_str[10];
    unsigned int devid;
	if (verbose>2) flsprintf(stdout,"getting ID for family %d\n",chip_family);
	p16c_read_page(mem_str, 0x3FFFFE*2,2);
    devid = (((unsigned int)(mem_str[1]))<<8) + (((unsigned int)(mem_str[0]))<<0);
    devid = devid & devid_mask;
    return devid;
	}


//*********************************************************************************//
//*********************************************************************************//
//*********************************************************************************//
//               hex parse and main function
//*********************************************************************************//
//*********************************************************************************//
//*********************************************************************************//


int parse_hex (char * filename, unsigned char * progmem, unsigned char * config)
    {
    char * line = NULL;
    unsigned char line_content[128];
    size_t len = 0;
    int i,temp, read,line_len, line_type, line_address, line_address_offset,effective_address;
    if (verbose>2) printf ("Opening filename %s \n", filename);
    FILE* sf = fopen(filename, "r");
    if (sf==0)
        return -1;
    line_address_offset = 0;
    if (verbose>2) printf ("File open\n");
    while ((read =  getlinex(&line, &len, sf)) != -1)
        {
        if (verbose>2) printf("\nRead %d chars: %s",read,line);
        if (line[0]!=':')
            {
            if (verbose>1) printf("--- : invalid\n");
            return -1;
            }
        sscanf(line+1,"%2X",&line_len);
        sscanf(line+3,"%4X",&line_address);
        sscanf(line+7,"%2X",&line_type);
        effective_address = line_address+(65536*line_address_offset);
        if (verbose>2) printf("Line len %d B, type %d, address 0x%4.4x offset 0x%4.4x, EFF 0x%6.6x\n",line_len,line_type,line_address,line_address_offset,effective_address);
        if (line_type==0)
            {
            for (i=0; i<line_len; i++)
                {
                sscanf(line+9+i*2,"%2X",&temp);
                line_content[i] = temp;
                }
            if (effective_address<flash_size)
                {
                if (verbose>2) printf("PM ");
                for (i=0; i<line_len; i++) progmem[effective_address+i] = line_content[i];
                }
            if (line_address_offset==0x30)
                {
                if (verbose>2) printf("CB ");
                for (i=0; i<line_len; i++) config[effective_address-0x300000+i] = line_content[i];
                }
            }
        if (line_type==4)
            {
            sscanf(line+9,"%4X",&line_address_offset);
            }
        if (verbose>2) for (i=0; i<line_len; i++) printf("%2.2X",line_content[i]);
        if (verbose>2) printf("\n");
        }
    fclose(sf);
    return 0;
    }


int main(int argc, char *argv[])
    {
    int i,j,pages_performed;
    unsigned char * pm_point, * cm_point;
    unsigned char tdat[200];
    parseArgs(argc,argv);
    if (verbose>0) printf ("PP programmer for Q43/Q8x, version %s\n",PP_VERSION3);
    if (verbose>1) printf ("Opening serial port\n");
    initSerialPort();
    if (sleep_time>0)
        {
        if (verbose>0) printf ("Sleeping for %d ms while arduino bootloader expires\n", sleep_time);
        fflush(stdout);
        sleep_ms (sleep_time);
        }

    for (i=0; i<PROGMEM_LEN; i++) progmem[i] = 0xFF;		//assume erased memories (0xFF)
    for (i=0; i<CONFIG_LEN; i++) config_bytes[i] = 0xFF;

    char* filename=argv[argc-1];
    pm_point = (unsigned char *)(&progmem);
    cm_point = (unsigned char *)(&config_bytes);
    parse_hex(filename,pm_point,cm_point);					//parse and write content of hex file into buffers

    //now this is ugly kludge
    //my original programmer expected only file_image holding the image of memory to be programmed
    //for PIC18, it is divided into two regions, program memory and config. to glue those two
    //different approaches, I made this. not particulary proud of having this mess
    for (i=0; i<70000; i++) file_image [i] = progmem[i];
    for (i=0; i<10; i++) file_image [2*0x8007 + i] = config_bytes[i];
//    for (i=0; i<10; i++) printf ("%2.2x",config_bytes[i]);
    for (i=0; i<70000; i++)
        {
        if ((i%2)!=0)
            file_image[i] = 0x3F&file_image[i];
        }

    prog_enter_progmode();									//enter programming mode and probe the target
    i = prog_get_device_id();
    if (i==devid_expected)
		{
        if (verbose>0) printf ("Device ID: %4.4x \n", i);
		}
    else
        {
        printf ("Wrong device ID: %4.4x, expected: %4.4x\n", i,devid_expected);
        printf ("Check for connection to target MCU, exiting now\n");
        prog_exit_progmode();
        return 1;
        }
        if (program==1)
            {
            pages_performed = 0;
                p16c_mass_erase();          //erase whole device
            if (verbose>0) printf ("Programming FLASH (%d B in %d pages per %d bytes): \n",flash_size,flash_size/page_size,page_size);
            fflush(stdout);
            for (i=0; i<flash_size; i=i+page_size)
                {
                if (is_empty(progmem+i,page_size)==0)
                    {
					p18q_write_page(progmem+i,i*2,page_size);

                    pages_performed++;
                    if (verbose>1)
						{
						printf ("#");
						fflush(stdout);
						}
                    }
                else if (verbose>2)
                    {
                    printf (".");
                    fflush(stdout);
                    }
                }

            if (verbose>0) printf ("\n%d pages programmed\n",pages_performed);
            if (verbose>0) printf ("Programming config\n");
            for (i=0; i<config_size; i++) //write config bytes
				{
                p18q_write_single_cfg (config_bytes[i],0x300000+i);
				}

            }
        if (verify==1)
            {
            pages_performed = 0;
            if (verbose>0) printf ("Verifying FLASH (%d B in %d pages per %d bytes): \n",flash_size,flash_size/page_size,page_size);
            for (i=0; i<flash_size; i=i+page_size)
                {
                if (is_empty(progmem+i,page_size))
                    {
                    if (verbose>2)
                        {
                        printf ("#");
                        fflush(stdout);
                        }
                    }
                else
                    {
					p16c_read_page(tdat,i*2,page_size);
                    pages_performed++;
                    if (verbose>3) printf ("Verifying page at 0x%4.4X\n",i);
                    if (verbose>1)
                        {
                        printf ("#");
                        fflush(stdout);
                        }
                    for (j=0; j<page_size; j++)
                        {
                        if (progmem[i+j] != tdat[j])
                            {
                            printf ("Error at 0x%4.4X E:0x%2.2X R:0x%2.2X\n",i+j,progmem[i+j],tdat[j]);
                            printf ("Exiting now\n");
                            prog_exit_progmode();
                            exit(0);
                            }
                        }
                    }
                }
            if (verbose>0) printf ("\n%d pages verified\n",pages_performed);
				p18q_read_cfg(tdat,0x300000,config_size);

			if (verbose>0) printf ("Verifying config...");
			for (i=0; i<config_size; i++)
				{
                if (config_bytes[i] != tdat[i])
                    {
                    printf ("Error at 0x%2.2X E:0x%2.2X R:0x%2.2X\n",i,config_bytes[i],tdat[i]);
                    printf ("Exiting now\n");
                    prog_exit_progmode();
                    exit(0);
					}
				}
			if (verbose>0) printf ("OK\n");
            }
    prog_exit_progmode();
    return 0;
    }

