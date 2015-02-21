#pragma once 

#define AT24_PACKET_SIZE 10

struct at24_op {
	uint8_t *data; 
	uint16_t size; // total length
	uint16_t addr; 
	uint8_t len; // current op length
	uint8_t buffer[AT24_PACKET_SIZE]; // i2c buffer
}; 

struct at24 {
	i2c_dev_t i2c;
	struct at24_op op; 
	struct pt thread; 
	struct pt bthread; 
	uint8_t status; 
};

/// initializes a new eeprom structure
void at24_init(struct at24 *self, i2c_dev_t i2c); 
/// aquires the eeprom for this thread
uint8_t at24_aquire(struct at24 *self); 
/// releases the eeprom for others
void 		at24_release(struct at24 *self); 
/// starts a write to the eeprom
int8_t at24_start_write(struct at24 *self, uint16_t addr, const uint8_t *buf, uint16_t count); 
/// starts a read sequence from the eeprom
int8_t at24_start_read(struct at24 *self, uint16_t addr, uint8_t *buf, uint16_t count); 
/// checks if eeprom transacton is in progress
uint8_t at24_busy(struct at24 *self); 
/// main eeprom tick routine
void at24_update(struct at24 *self); 
