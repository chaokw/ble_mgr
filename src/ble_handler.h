#ifndef	BLE_HANDLER_H
#define	BLE_HANDLER_H

#include <time.h>
#include "ble_mgr.h"

/*****************************************************************************
* Common Response types
*****************************************************************************/
typedef enum MsgRespT
{
    RESP_OK          =  0,  
    RESP_ERROR       = -1,  
} MsgRespT;


/*****************************************************************************
 * Common Response Header Format
 *****************************************************************************/
typedef struct CommonRspHdrT 
{
    CompMsgHeaderT   msgHdr;     /* Common Message Header */
    unsigned char      code;       /* Return Code */
} CommonRspHdrT;

#endif /* #ifndef BLE_HANDLER_H */



