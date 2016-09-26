#ifndef	BLE_MGR_H
#define	BLE_MGR_H

#define UNUSED_ARG  __attribute__((unused))
#include <termios.h>
#include <unistd.h>

/***********************************************************
 *  Maximum Path File Name Size
 ***********************************************************/
#define MAX_PATH_NAME 255

#define BAUDRATE B115200
#define MODEMDEVICE "/dev/ttyS0"
#define TIMEFORMAT "%Y-%m-%d %H:%M:%S"

#define GLOBAL_RECEIVE_BUFFER_SIZE 512
#define BUFSIZE         64
#define HCOLS           20
#define ICOLS           18

#define MODE_START_DATE  0
#define MODE_DATE        1
#define MODE_START_TEXT  2
#define MODE_TEXT        3
#define MODE_HEX         5

#define DISABLE          0
#define ENABLE           1


typedef enum BLEErrorT
{
    BLE_OK                =   0,
    BLE_ERROR           =  -1,
    BLE_NOMEM          =  -2,  /* Insufficient memory */
    BLE_INUSE            =  -3,  /* Resources are currently in use and cannot be modified or deleted */
    BLE_INVALIDDATA   =  -4,
    BLE_LAST_ERR      =  BLE_INVALIDDATA
}BLEErrorT;


typedef struct BLEMgmtTaskCtxT
{
    Bool running;              /* Controls execution state.  Setting to false causes shutdown. */
    char *programName;         /* A binary string that identifies the BLEMgmt process. */
    struct
    {
        S32  debug;                      /* Run the BLEMgmt with debug */
        char pidFile[MAX_PATH_NAME];     /* Path to the pid file */
    } cfg;                               /* BLEMgmt process configuration */
    struct
    {
        struct termios options;
        fd_set mask;
        speed_t speed;  /* uart baud rate */
        U8  mode;  /* when bebug enabled, decide print mode */
        int  fd;  
    } uart;
    U8 loglevel;    /* BLEMgmt Logging context */
    MsgQT  msgq; /*Message queue used by the BLEMgmt */	
} BLEMgmtTaskCtxT;



#endif /* #ifndef BLE_MGR_H */

