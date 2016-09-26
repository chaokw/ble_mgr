/* --- system includes ---*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>

/* --- project includes ---*/
#include "msgq.h"
#include "dbgMsgq.h"
#include "ble_mgr.h"
#include "ble_handler.h"

/* --- local static constants ---*/
static unsigned char rxbuf[256];
static unsigned char received_buffer[GLOBAL_RECEIVE_BUFFER_SIZE];


/*****************************************************************************
 * key path for shared memory
 *****************************************************************************/
#define KEY_PATH "/usr/lib/libmsgq.so"

extern BLEMgmtTaskCtxT  *gpBLEMgmtTaskCtx;


static void print_hex_line(unsigned char *prefix, unsigned char *outbuf, int index)
{
  int i;

  printf("\r%s", prefix);
  for(i = 0; i < index; i++) {
    if((i % 4) == 0) {
      printf(" ");
    }
    printf("%02X", outbuf[i] & 0xFF);
  }
  printf("  ");
  for(i = index; i < HCOLS; i++) {
    if((i % 4) == 0) {
      printf(" ");
    }
    printf("  ");
  }
  for(i = 0; i < index; i++) {
    if(outbuf[i] < 30 || outbuf[i] > 126) {
      printf(".");
    } else {
      printf("%c", outbuf[i]);
    }
  }
}


/*****************************************************************************
* Name:MsgProcInit
* Description:sets up the BLE management message process thread configuration, 
*                  and initializes anything else needed by this. 
* Input param:
* Output param:
* Return-Value:
* MSG_OK - Function completed successfully
* MSG_ERROR - Memory allocation failure occurred
******************************************************************************/ 
MsgErrorT MsgProcInit(void)
{
    MsgErrorT rc = MSG_OK;

    do
    {
        /* Initialize the BLEMgmt message queue. */
        rc = MsgQInit(&gpBLEMgmtTaskCtx->msgq, MSGQ_ID_AP_MANAGEMENT);
        if (rc != MSG_OK)
        {
            printf("MsgQInit failed, rc=%d.\n", rc);
            break;
        }
    } while(0);

    return rc;
}


/*****************************************************************************
* Name:BLEProcThread
* Description:This function sets up the message process thread configuration, then
*                  enters the main thread loop.
* Input param:
* ptr - Unused argument (e.g., UNUSED_ARG)
* Output param:
* Return-Value:
******************************************************************************/
void *BLEProcThread( UNUSED_ARG void *ptr )
{   
    /* do some init, such as message queue init ... */
    MsgProcInit();
    BLEProcMain(gpBLEMgmtTaskCtx);
    return NULL;
}


/*****************************************************************************
* Name:BLEProcMain
* Description:This function is main thread loop of message processing. 
* Input param:
* BLEMgmt - Pointer to the BLEMgmtTaskCtxT
* Output param:
* Return-Value:
* MSG_OK - if everything worked as expected
* MSG_ERROR - if an error occurs
******************************************************************************/
BLEErrorT BLEProcMain(BLEMgmtTaskCtxT *BLEMgmt)
{
    BLEErrorT rc;
    MsgQMessageT *msg = NULL;
    int nfound;
    int index = 0;	
    unsigned char buf[BUFSIZE], outbuf[HCOLS];
    char *timeformat = TIMEFORMAT;
	
    if (NULL == BLEMgmt)
    {
        printf("BLEMgmt is exiting due to invalid control context.\n");
        return BLE_ERROR;
    }

    ble_timer_init();   //chaow

    /* Main BLEMgmt Loop */
    printf("BLEMgmt process is running.\n");

    while (BLEMgmt->running == TRUE)
    {
       nfound = select(FD_SETSIZE, &gpBLEMgmtTaskCtx->uart.mask, (fd_set *) 0, (fd_set *) 0, (struct timeval *) 0);
       if(nfound < 0) {
         if(errno == EINTR) {
           fprintf(stderr, "interrupted system call\n");
           continue;
         }
         perror("select");
         exit(1);
       }


#if 0  //chaokw
       if(FD_ISSET(fileno(stdin), &gpBLEMgmtTaskCtx->uart.mask)) {
             int n = read(fileno(stdin), buf, sizeof(buf));
             if(n < 0) {
               perror("could not read");
               exit(-1);
             } else if(n > 0) {
               if(n > 0) {
                 int i;
                 for(i = 0; i < n; i++) {
                   if(write(gpBLEMgmtTaskCtx->uart.fd, &buf[i], 1) <= 0) {
                     perror("write");
                     exit(1);
                   } else {
                     fflush(NULL);
                     usleep(6000);
                   }
                 }
               }
             } else {
               exit(0);
             }
           }
#endif


           if(FD_ISSET(gpBLEMgmtTaskCtx->uart.fd, &gpBLEMgmtTaskCtx->uart.mask)) {
             /* Get buffer */
             int i, j, n = read(gpBLEMgmtTaskCtx->uart.fd, buf, sizeof(buf));
             if(n < 0) {
               perror("could not read");
               exit(-1);
             }

              /* If debug is enabled, print uart data */
             if(ENABLE == gpBLEMgmtTaskCtx->cfg.debug ) {
             //if(1) {
              for(i = 0; i < n; i++) {
                switch(gpBLEMgmtTaskCtx->uart.mode) {
                  case MODE_START_DATE: {
                    time_t t;
                    t = time(&t);
                    strftime(outbuf, HCOLS, timeformat, localtime(&t));
                    printf("%s|", outbuf);
                    gpBLEMgmtTaskCtx->uart.mode = MODE_DATE;
                  }
                  case MODE_DATE:
                    printf("%c", buf[i]);
                    if(buf[i] == '\n') {
                      gpBLEMgmtTaskCtx->uart.mode = MODE_START_DATE;
                    }
                    break;
                  case MODE_HEX:
                    rxbuf[index++] = buf[i];
                    if(index >= HCOLS) {
                      print_hex_line("", rxbuf, index);
                      index = 0;
                      printf("\n");
                    }
                    break;
                }
              }

              if(index > 0) {
                switch(gpBLEMgmtTaskCtx->uart.mode) {
                  case MODE_HEX:
                    print_hex_line("", rxbuf, index);
                    break;
                }
              }
              fflush(stdout);
              }

              /* received content  */  
              L1_receive_data(buf, n);

			 
             }
    }

    /* Clean up the message queue */
    MsgQShutdown(&BLEMgmt->msgq);

    /* Thread is exiting */
    printf("BLEMgmt message process is exiting.\n");
    return BLE_OK;
}




