/* --- system includes ---*/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>

/* --- project includes ---*/
#include "msgq.h"
#include "ble_mgr.h"

extern char *optarg;
extern int optind, opterr, optopt;
extern void *BLEProcThread( UNUSED_ARG void *ptr );

/*********************************************************
 * BLEMgmt Controller task context
 *********************************************************/
BLEMgmtTaskCtxT  gBLEMgmtTaskCtx;

/*********************************************************
 * Global pointer to the BLEMgmt Controller task context
 *********************************************************/
BLEMgmtTaskCtxT  *gpBLEMgmtTaskCtx = NULL;

 /*****************************************************************************
* Name:Usage
* Description:Prints out switch usage information.
* Input param:
* progName - Name of the program which was invoked
* Output param:
* Return-Value:
*****************************************************************************/
static void Usage(const char *progName)
{
    printf("Usage: %s [options]\n", progName );
    printf("  -d             Run the program in debug mode in the forground, don't daemonize\n");
    printf("  -l <num>       Startup log level\n");
    printf("  -p <file>      Write the pid to the specified file\n");
    printf("  -x             For hexadecimal output\n");
    printf("  -t             To add time for each text line\n");
    printf("  -b<57600|115200>   Set uart baud rate\n");
    return;
}

 /*****************************************************************************
* Name:ProcessCmdlineOptions
* Description:Parse the command line options.
* Input param:
* argc - Number of arguments
* argv - Pointer to the argument list
* Output param:
* Return-Value:
*****************************************************************************/
static void ProcessCmdlineOptions(S32 argc, char *argv[])
{
    S32 c;
    Bool err = FALSE;
    opterr = 0;

    while ((c = getopt (argc, argv, "dl:p:xtb:")) != -1)
    {
        switch (c)
        {
            case 'd':
                gpBLEMgmtTaskCtx->cfg.debug = ENABLE;
                break;
            case 'l':
                gpBLEMgmtTaskCtx->loglevel = strtol(optarg, NULL, 10);
                break;
            case 'p':
                strncpy(gpBLEMgmtTaskCtx->cfg.pidFile, optarg,
                sizeof(gpBLEMgmtTaskCtx->cfg.pidFile)-1);
                gpBLEMgmtTaskCtx->cfg.pidFile[sizeof(gpBLEMgmtTaskCtx->cfg.pidFile)-1] = '/0';
                break;
            case 'x':
                gpBLEMgmtTaskCtx->uart.mode = MODE_HEX;
                gpBLEMgmtTaskCtx->cfg.debug = ENABLE;
                break;
            case 't':
                gpBLEMgmtTaskCtx->uart.mode = MODE_START_DATE;
                gpBLEMgmtTaskCtx->cfg.debug = ENABLE;
                break;				
            case 'b':
	          /* set uart speed */
                if(strcmp(optarg, "57600") == 0) {
                  gpBLEMgmtTaskCtx->uart.speed  = B57600;
                  //speedname = "57600";
                } else if(strcmp(optarg, "115200") == 0) {
                  gpBLEMgmtTaskCtx->uart.speed  = B115200;
                  //speedname = "115200";
                } else {
                  fprintf(stderr, "unsupported speed: %s\n", &optarg);
                  return Usage(gpBLEMgmtTaskCtx->programName);
                }				
                break;
				
            case '?':
            default:
                fprintf(stderr, "%s: Command line argument '%c' is not supported\n",
                        gpBLEMgmtTaskCtx->programName, optopt);
                err = TRUE;
                break;
        }
    }
    if (err == TRUE)
    {
        Usage(gpBLEMgmtTaskCtx->programName);
        exit(1);
    }
    return;
}

 /*****************************************************************************
* Name:TaskCtxInit
* Description:BLEMgmt task initialization routines.
* Input param:
* Output param:
* Return-Value:
******************************************************************************/
static void TaskCtxInit(void)
{
    /*
     * Initialize the BLEMgmt task context
     */
    memset(&gBLEMgmtTaskCtx, 0, sizeof(gBLEMgmtTaskCtx));
    gpBLEMgmtTaskCtx = &gBLEMgmtTaskCtx;
    gpBLEMgmtTaskCtx->running = TRUE;
    gpBLEMgmtTaskCtx->programName = "BLEMgmt";

    gpBLEMgmtTaskCtx->uart.speed = BAUDRATE;
    gpBLEMgmtTaskCtx->uart.mode = MODE_START_TEXT;
    gpBLEMgmtTaskCtx->cfg.debug = DISABLE;

}


 /*****************************************************************************
* Name:UartInit
* Description:UART initialization routines.
* Input param:
* Output param:
* Return-Value:
******************************************************************************/
static void UartInit(char *device)
{
    /*
     * Initialize the UART context
     */
    char *speedname;
    fd_set localmask;

    if (B115200 == gpBLEMgmtTaskCtx->uart.speed)
    {
    	speedname = "115200";
    }
    else if(B57600 == gpBLEMgmtTaskCtx->uart.speed)
    {
       speedname = "57600";
    }
	
    fprintf(stderr, "connecting to %s (%s)", device, speedname);
    gpBLEMgmtTaskCtx->uart.fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY | O_SYNC );
    if(gpBLEMgmtTaskCtx->uart.fd < 0) {
      fprintf(stderr, "\n");
      perror("open");
      exit(-1);
    }
    fprintf(stderr, " [OK]\n");

    if(fcntl(gpBLEMgmtTaskCtx->uart.fd, F_SETFL, 0) < 0) {
      perror("could not set fcntl");
      exit(-1);
    }

    if(tcgetattr(gpBLEMgmtTaskCtx->uart.fd, &gpBLEMgmtTaskCtx->uart.options) < 0) {
      perror("could not get options");
      exit(-1);
    }

    cfsetispeed(&gpBLEMgmtTaskCtx->uart.options, gpBLEMgmtTaskCtx->uart.speed);
    cfsetospeed(&gpBLEMgmtTaskCtx->uart.options, gpBLEMgmtTaskCtx->uart.speed);
    /* Enable the receiver and set local mode */
    gpBLEMgmtTaskCtx->uart.options.c_cflag |= (CLOCAL | CREAD);
    /* Mask the character size bits and turn off (odd) parity */
    gpBLEMgmtTaskCtx->uart.options.c_cflag &= ~(CSIZE | PARENB | PARODD);
    /* Select 8 data bits */
    gpBLEMgmtTaskCtx->uart.options.c_cflag |= CS8;
    /* Raw input */
    gpBLEMgmtTaskCtx->uart.options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    /* Raw output */
    gpBLEMgmtTaskCtx->uart.options.c_oflag &= ~OPOST;

    if(tcsetattr(gpBLEMgmtTaskCtx->uart.fd, TCSANOW, &gpBLEMgmtTaskCtx->uart.options) < 0) {
      perror("could not set options");
      exit(-1);
    }

    FD_ZERO(&localmask);
    FD_SET(gpBLEMgmtTaskCtx->uart.fd, &localmask);
    //FD_SET(fileno(stdin), &localmask);    //chaokw
	
    gpBLEMgmtTaskCtx->uart.mask =  localmask;

}

 /*****************************************************************************
* Name:SyslogiInit
* Description:syslog initialization routines.
* Input param:
* Output param:
* Return-Value:
******************************************************************************/
static void SyslogiInit(void)
{
    /*
     * Initialize the syslog
     */
}

 /*****************************************************************************
* Name:DbgCliStart
* Description:debug cli initialization routines.
* Input param:
* Output param:
* Return-Value:
******************************************************************************/
static void DbgCliStart(void)
{
    /*
     * Initialize the syslog
     */
}

 /*****************************************************************************
* Name:localShutdown
* Description:Signal handler for shutting down.
* Input param:
* sig -UNUSED_ARG
* Output param:
* Return-Value:
******************************************************************************/
static void localShutdown ( UNUSED_ARG int sig )
{
    /* Signal the BLEMgmt to shutdown. */
    printf("local shutdown\r\n");
    gpBLEMgmtTaskCtx->running = FALSE;
    MsgQPendCancel(&gpBLEMgmtTaskCtx->msgq); 
	
    exit(0);
}


#ifndef BLEMGMT_UNIT_TEST
/*****************************************************************************
* Name:main
* Description:Main loop for BLE management application.
* Input param:
* argc - Number of arguments
* argv - Pointer to the argument list
* Output param:
* Return-Value:
* zero - for normal exit
* non -zero for abnormal exit
******************************************************************************/
S32 main(S32 argc, char *argv[])
{
    FILE       *pidFile         = NULL;
    pthread_t   BLEProcThreadCtx;

    /* Set up signal handlers for catching ctrl-c */
    signal(SIGTERM, localShutdown);
    signal(SIGQUIT, localShutdown);
    signal(SIGINT,  localShutdown);
    signal(SIGPIPE, SIG_IGN);
    //signal(SIGSEGV, SegvHandler);     //chaokw
    /* to have a backtrace incase assert trigger a SIGABRT    glic  eglic support ? */	
    //signal(SIGABRT, SegvHandler);  
	
    /* Initialize the task control blocks */
    TaskCtxInit();

    /* BLEMgmt Logging Initialization */
    SyslogiInit();

    /* Start the debug cli if needed ? */
    DbgCliStart();	

     /* Parse command line options */
    ProcessCmdlineOptions(argc, argv);

     /* init uart related config */
    UartInit(MODEMDEVICE);

#if 0   //chaokw  add printf
    if ( gpBLEMgmtTaskCtx->cfg.debug == ENABLE )
    {
        //SerialDump();
    }
#endif	

    /* Write out the pid file */
    if ( strlen(gpBLEMgmtTaskCtx->cfg.pidFile) > 0 )
    {
        pidFile = fopen(gpBLEMgmtTaskCtx->cfg.pidFile, "w");
        if ( pidFile == NULL )
        {
            fprintf(stderr, "Failed open pid file (errno=%s)\n", strerror(errno));
        }
        fprintf(pidFile, "%d", getpid());
        fclose(pidFile);
    }
    /* Start the message process thread for AP management */
    pthread_create( &BLEProcThreadCtx, NULL, BLEProcThread, NULL);
    		
    /* Enter the main task loop to do other things*/
    //BLEMgmtMain();     //chaokw

    /* No need to cancel this thread as it will exit on it's own. */
    pthread_join(BLEProcThreadCtx, NULL);
    return 0;   

}	

#endif
