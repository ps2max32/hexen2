/* net_ps2.c: the network drivers: loopback only (the local game's client
   and server), no LAN drivers: maxsdk has no sockets. As uHexen2's
   net_bsd.c without the datagram driver. */

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"
#include "net_defs.h"
#include "net_loop.h"

net_driver_t net_drivers[] = {
	{	"Loopback",
		false,
		Loop_Init,
		Loop_Listen,
		Loop_SearchForHosts,
		Loop_Connect,
		Loop_CheckNewConnections,
		Loop_GetMessage,
		Loop_SendMessage,
		Loop_SendUnreliableMessage,
		Loop_CanSendMessage,
		Loop_CanSendUnreliableMessage,
		Loop_Close,
		Loop_Shutdown
	}
};
const int net_numdrivers = Q_COUNTOF(net_drivers);

/* none: an entry only for the array's sake */
net_landriver_t net_landrivers[1];
const int net_numlandrivers = 0;
