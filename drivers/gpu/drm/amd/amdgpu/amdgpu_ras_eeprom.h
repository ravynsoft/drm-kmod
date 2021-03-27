/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _AMDGPU_RAS_EEPROM_H
#define _AMDGPU_RAS_EEPROM_H

#include <linux/i2c.h>

struct amdgpu_device;

enum amdgpu_ras_eeprom_err_type{
	AMDGPU_RAS_EEPROM_ERR_PLACE_HOLDER,
	AMDGPU_RAS_EEPROM_ERR_RECOVERABLE,
	AMDGPU_RAS_EEPROM_ERR_NON_RECOVERABLE
};

struct amdgpu_ras_eeprom_table_header {
	uint32_t header;
	uint32_t version;
	uint32_t first_rec_offset;
	uint32_t tbl_size;
	uint32_t checksum;
}__attribute__((__packed__));

struct amdgpu_ras_eeprom_control {
	struct amdgpu_ras_eeprom_table_header tbl_hdr;
	u32 i2c_address; /* Base I2C 19-bit memory address */
	uint32_t next_addr;
	unsigned int num_recs;
	struct mutex tbl_mutex;
	u8 tbl_byte_sum;
};

/*
 * Represents single table record. Packed to be easily serialized into byte
 * stream.
 */
struct eeprom_table_record {

	union {
		uint64_t address;
		uint64_t offset;
	};

	uint64_t retired_page;
	uint64_t ts;

	enum amdgpu_ras_eeprom_err_type err_type;

	union {
		unsigned char bank;
		unsigned char cu;
	};

	unsigned char mem_channel;
	unsigned char mcumc_id;
}__attribute__((__packed__));

int amdgpu_ras_eeprom_init(struct amdgpu_ras_eeprom_control *control,
			bool *exceed_err_limit);
int amdgpu_ras_eeprom_reset_table(struct amdgpu_ras_eeprom_control *control);

bool amdgpu_ras_eeprom_check_err_threshold(struct amdgpu_device *adev);

int amdgpu_ras_eeprom_read(struct amdgpu_ras_eeprom_control *control,
			   struct eeprom_table_record *records, const u32 num);

int amdgpu_ras_eeprom_write(struct amdgpu_ras_eeprom_control *control,
			    struct eeprom_table_record *records, const u32 num);

#ifdef __linux__
inline uint32_t amdgpu_ras_eeprom_get_record_max_length(void);
#elif defined(__FreeBSD__)
uint32_t amdgpu_ras_eeprom_get_record_max_length(void);
#endif

void amdgpu_ras_eeprom_test(struct amdgpu_ras_eeprom_control *control);

#endif // _AMDGPU_RAS_EEPROM_H
