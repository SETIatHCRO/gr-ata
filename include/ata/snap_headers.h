#ifndef __ATA_SNAP_HEADERS_H__
#define __ATA_SNAP_HEADERS_H__

#define SNAP_PACKETTYPE_NONE 0
#define SNAP_PACKETTYPE_VOLTAGE 1
#define SNAP_PACKETTYPE_SPECT 2

#include <stdint.h>

namespace gr {
namespace ata {
	struct snap_header {
		// Antenna id: A runtime configurable ID which uniquely associates a packet with a particular SNAP board and antenna
		// channel_id: Index of the first channel present in the packet.  channels will be id to id + 255
		// sample_number: index of the first time sample present.  16 time slices will be in each packet.
		// 				  So the samples in this packet will be sample_number to sample_number + 15
		uint8_t antenna_id :6;
		uint16_t channel_id :12;
		uint64_t sample_number : 38;
		uint8_t firmware_version : 8;
	};

} // namespace ata
} // namespace gr

#endif
