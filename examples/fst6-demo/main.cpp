/*
Example for the fst6 radio transmitter board 

*/ 

#include <kernel.h>
#include <boards/rc/flysky-t6-tx.h>
#include <tty/vt100.h>
#include <gui/m2_fb.h>
#include "gui.hpp" 

extern char _sdata; 

#define DEMO_STATUS_RD_CONFIG (1 << 0)
#define DEMO_STATUS_WR_CONFIG (1 << 1)

typedef enum {
	IO_WRITE, 
	IO_READ
} io_direction_t; 

struct block_transfer {
	uint8_t completed; 
	ssize_t address; 
	ssize_t transfered; 
	ssize_t size; 
	uint8_t *buffer; 
	io_direction_t dir; 
	block_dev_t dev; 
}; 

void blk_transfer_init(struct block_transfer *tr, 
	block_dev_t dev, uint32_t address, uint8_t *buffer, uint32_t size, io_direction_t dir){
	tr->completed = 0; 
	tr->address = address; 
	tr->transfered = 0; 
	tr->size = size; 
	tr->buffer = buffer; 
	tr->dir = dir; 
	tr->dev = dev; 
}

uint8_t blk_transfer_completed(struct block_transfer *tr){
	if(tr->completed == 1) {
		 return 1; 
	} else if(tr->transfered < tr->size){
		ssize_t transfered = 0; 
		
		if(tr->dir == IO_WRITE)
			transfered = blk_writepage(tr->dev, tr->address + tr->transfered, tr->buffer + tr->transfered, tr->size - tr->transfered); 
		else
			transfered = blk_readpage(tr->dev, tr->address + tr->transfered, tr->buffer + tr->transfered, tr->size - tr->transfered); 
			
		if(transfered > 0) {
			printf("Transfered %d bytes of %d\n", transfered, tr->size); 
			tr->transfered += transfered; 
		}
	} else if(tr->transfered == tr->size && !blk_get_status(tr->dev, BLKDEV_BUSY)){
		tr->completed = 1; 
		return 1; 
	}
	
	return 0; 
}

#define CHANNEL_FLAG_REVERESED (1 << 0)

struct channel_config {
	uint8_t source; 
	uint16_t min; 
	uint16_t max; 
	uint8_t rate; 
	uint8_t exponent; 
	uint8_t flags; 
	int16_t offset; 
}; 

struct config {
	struct channel_config channels[6]; 
	uint16_t checksum; 
}; 

const struct config default_config = {
	.channels = {
		{.source = 0, .min = 1000, .max = 2000, .rate = 100, .exponent = 0, .flags = 0, .offset = 0},
		{.source = 1, .min = 1000, .max = 2000, .rate = 100, .exponent = 0, .flags = 0, .offset = 0},
		{.source = 2, .min = 1000, .max = 2000, .rate = 100, .exponent = 0, .flags = 0, .offset = 0},
		{.source = 3, .min = 1000, .max = 2000, .rate = 100, .exponent = 0, .flags = 0, .offset = 0},
		{.source = 4, .min = 1000, .max = 2000, .rate = 100, .exponent = 0, .flags = 0, .offset = 0},
		{.source = 5, .min = 1000, .max = 2000, .rate = 100, .exponent = 0, .flags = 0, .offset = 0}
	}, 
	.checksum = 0
}; 

uint16_t fletcher16( uint8_t const *data, size_t bytes ){
	uint16_t sum1 = 0xff, sum2 = 0xff;

	while (bytes) {
		size_t tlen = bytes > 20 ? 20 : bytes;
		bytes -= tlen;
		do {
						sum2 += sum1 += *data++;
		} while (--tlen);
		sum1 = (sum1 & 0xff) + (sum1 >> 8);
		sum2 = (sum2 & 0xff) + (sum2 >> 8);
	}
	/* Second reduction step to reduce sums to 8 bits */
	sum1 = (sum1 & 0xff) + (sum1 >> 8);
	sum2 = (sum2 & 0xff) + (sum2 >> 8);
	return sum2 << 8 | sum1;
}

uint8_t config_valid(struct config *self){
	return self->checksum == fletcher16((uint8_t*)self, sizeof(struct config) - sizeof(self->checksum)); 
}

void config_update_checksum(struct config *self){
	self->checksum = fletcher16((uint8_t*)self, sizeof(struct config) - sizeof(self->checksum));
}

uint16_t config_calc_checksum(struct config *self){
	return fletcher16((uint8_t*)self, sizeof(struct config) - sizeof(self->checksum));
}

struct application {
	struct libk_thread main_thread; 
	struct block_transfer tr; 
	block_dev_t eeprom; 
	struct config conf; 
	uint8_t status; 
}; 

static struct application app; 
static struct gui_data *gui; 

LIBK_THREAD(config_thread){
	
	PT_BEGIN(pt); 
	
	while(1){
		static struct block_device_geometry geom; 
		blk_get_geometry(app.eeprom, &geom); 
		
		//printf("Using storage device %dkb, %d sectors, %d pages/sector, %d bytes per page\n", 
		//	geom.pages * geom.page_size, geom.sectors, geom.pages / geom.sectors, geom.page_size); 
			
		PT_WAIT_UNTIL(pt, app.status & (DEMO_STATUS_WR_CONFIG | DEMO_STATUS_RD_CONFIG)); 
		
		if(app.status & DEMO_STATUS_WR_CONFIG){
			printf("CONF: write\n"); 
			
			PT_WAIT_UNTIL(pt, blk_open(app.eeprom)); 
			
			static uint16_t j = 0; 
			for(j = 0; j < (sizeof(struct config) / geom.page_size); j++){
				printf("CONF: write page #%d\n", j); 
				blk_transfer_init(&app.tr, app.eeprom, (j * geom.page_size), ((uint8_t*)&app.conf) + (j * geom.page_size), geom.page_size, IO_WRITE); 
				PT_WAIT_UNTIL(pt, blk_transfer_completed(&app.tr)); 
			}
			
			blk_close(app.eeprom); 
			
			printf("CONF: saved\n"); 
			app.status &= ~DEMO_STATUS_WR_CONFIG; 
		} 
		if(app.status & DEMO_STATUS_RD_CONFIG){
			printf("CONF: read\n"); 
			
			PT_WAIT_UNTIL(pt, blk_open(app.eeprom)); 
			
			static uint16_t j = 0; 
			for(j = 0; j < (sizeof(struct config) / geom.page_size); j++){
				printf("CONF: read page #%d\n", j); 
				blk_transfer_init(&app.tr, app.eeprom, (j * geom.page_size), ((uint8_t*)&app.conf) + (j * geom.page_size), geom.page_size, IO_READ); 
				PT_WAIT_UNTIL(pt, blk_transfer_completed(&app.tr)); 
			}
			
			printf("CONF: loaded\n"); 
			
			blk_close(app.eeprom); 
			
			app.status &= ~DEMO_STATUS_RD_CONFIG;
		}
	}
	
	PT_END(pt); 
}

LIBK_THREAD(_console){
	static serial_dev_t serial = 0; 
	if(!serial) serial = uart_get_serial_interface(0); 
	PT_BEGIN(pt); 
	while(1){
		PT_WAIT_WHILE(pt, uart_getc(0) == SERIAL_NO_DATA); 
		libk_print_info(); 
		PT_YIELD(pt); 
	}
	PT_END(pt); 
}

// valign: bottom, halign: center, display width
/*======================================================================*/

void generic_root_change_cb(m2_rom_void_p new_root, m2_rom_void_p old_root, uint8_t change_value)
{
  printf("%p->%p %d\n", old_root, new_root, change_value);
}

LIBK_THREAD(output_thread){
	PT_BEGIN(pt); 
	
	while(1){
		// read raw values for each channel
		uint16_t outputs[6]; 
		for(int c = 0; c < 6; c++) {
			uint16_t raw = 0; 
			struct channel_config *cc = &app.conf.channels[c]; 
			switch(cc->source){
				case 0: 
				case 1: 
				case 2: 
				case 3: 
				case 4: 
				case 5: {
					raw = fst6_read_stick((fst6_stick_t)cc->source); 
				} break; 
				case 6: 
					raw = (fst6_key_down(FST6_KEY_SWA))?1000:2000; break; 
				case 7: 
					raw = (fst6_key_down(FST6_KEY_SWB))?1000:2000; break; 
				case 8: 
					raw = (fst6_key_down(FST6_KEY_SWC))?1000:2000; break; 
				case 9: 
					raw = (fst6_key_down(FST6_KEY_SWD))?1000:2000; break; 
				default: 
					raw = 1000; 
			}
			outputs[c] = cc->offset + map(1000 + (raw >> 2), cc->min, cc->max, 1000, 2000); 
			gui->out[c].value = outputs[c]; 
		}
		// do mixing here
		// ..
		
		// write outputs
		fst6_write_ppm(outputs[0], outputs[1], outputs[2], outputs[3], outputs[4], outputs[5]); 
			
		PT_YIELD(pt); 
	}
	
	PT_END(pt); 
}

LIBK_THREAD(main_thread){
	//serial_dev_t screen = fst6_get_screen_serial_interface(); 
	//fst6_key_mask_t keys = fst6_read_keys(); 
	int16_t key = 0; 
	
	PT_BEGIN(pt); 
	
	// load the config and wait for it to be loaded
	app.status |= DEMO_STATUS_RD_CONFIG; 
	PT_WAIT_WHILE(pt, app.status & DEMO_STATUS_RD_CONFIG); 
	if(!config_valid(&app.conf)){
		printf("CONF: invalid checksum %x, expected %x\n", app.conf.checksum, config_calc_checksum(&app.conf)); 
		memcpy(&app.conf, &default_config, sizeof(struct config)); 
		config_update_checksum(&app.conf); 
		app.status |= DEMO_STATUS_WR_CONFIG; 
		PT_WAIT_WHILE(pt, app.status & DEMO_STATUS_WR_CONFIG); 
	}
	
	while(1){
		while((key = fst6_read_key()) > 0){
			if(!(FST6_KEY_FLAG_UP & key)){
				switch(key & FST6_KEY_MASK){
					case FST6_KEY_OK: 
						m2_fb_put_key(M2_KEY_SELECT); 
						break; 
					case FST6_KEY_SELECT: 
						m2_fb_put_key(M2_KEY_EXIT); 
						break; 
					case FST6_KEY_CANCEL: 
						m2_fb_put_key(M2_KEY_DATA_DOWN); 
						break; 
					case FST6_KEY_ROTA: 
						m2_fb_put_key(M2_KEY_NEXT); 
						break; 
					case FST6_KEY_ROTB: 
						m2_fb_put_key(M2_KEY_PREV); 
						break; 
					case FST6_KEY_SWA: 
						gui->sw[0] = 0; 
						break; 
					case FST6_KEY_SWB: 
						gui->sw[1] = 0; 
						break; 
					case FST6_KEY_SWC: 
						gui->sw[2] = 0; 
						break; 
					case FST6_KEY_SWD: 
						gui->sw[3] = 0; 
						break; 
				}
				printf("KEY: %d\n", key & FST6_KEY_MASK); 
			} else {
				printf("DOW: %d\n", key & FST6_KEY_MASK); 
				switch(key & FST6_KEY_MASK){
					case FST6_KEY_SWA: 
						gui->sw[0] = 1; 
						break; 
					case FST6_KEY_SWB: 
						gui->sw[1] = 1; 
						break; 
					case FST6_KEY_SWC: 
						gui->sw[2] = 1; 
						break; 
					case FST6_KEY_SWD: 
						gui->sw[3] = 1; 
						break; 
				}
			}
		}
		/*
		for(int c = 0; c < 6; c++) {
			gui->ch[c].value = (int)fst6_read_stick((fst6_stick_t)c); 
			
			if(!gui->calibrated){
				gui->ch[c].value = 1000 + (gui->ch[c].value >> 2); 
			
				if(gui->ch[c].value < gui->ch[c].min)
					gui->ch[c].min = gui->ch[c].value; 
				if(gui->ch[c].value > gui->ch[c].max)
					gui->ch[c].max = gui->ch[c].value; 
			} else {
				gui->ch[c].value = map(1000 + (gui->ch[c].value >> 2), 
					gui->ch[c].min, gui->ch[c].max, 1000, 2000); 
			}
		}
		*/
		PT_YIELD(pt); 
		/*continue; 
		{
			timestamp_t t = timestamp_now(); 
			
			serial_printf(screen, "\x1b[2J\x1b[1;1H"); 
			serial_printf(screen, " FlySky FS-T6 %dMhz\n", (SystemCoreClock / 1000000UL)); 
			
			//serial_printf(screen, "%s\n", (char*)buf); 
			
			for(int c = 0; c < 6; c+=2) {
				sticks[c] = (int)fst6_read_stick((fst6_stick_t)c); 
				sticks[c+1] = (int)fst6_read_stick((fst6_stick_t)(c+1)); 
				sticks[c] = 1000 + (sticks[c] >> 2); 
				sticks[c+1] = 1000 + (sticks[c+1] >> 2); 
				
				serial_printf(screen, "CH%d: %04d CH%d: %04d\n", 
					c, (int)sticks[c], 
					c + 1, (int)sticks[c+1]); 
			}
			// write ppm
			fst6_write_ppm(sticks[0], sticks[1], sticks[2], sticks[3], sticks[4], sticks[5]); 
			
			serial_printf(screen, "VBAT: %d\n", (int)fst6_read_battery_voltage()); 
			
			serial_printf(screen, "Keys: "); 
			for(int c = 0; c < 32; c++){
				// play key sounds. 25ms long, 300hz
				if(keys & (1 << c) && !_key_state[c]){// key is pressed 
					fst6_play_tone(300, 25); 
					_key_state[c] = 1; 
				} else if(!(keys & (1 << c)) && _key_state[c]){ // released
					_key_state[c] = 0; 
				}
				if(keys & (1 << c)){
					serial_printf(screen, "%d ", c); 
				}
			}
			t = timestamp_ticks_to_us(timestamp_now() - t); 
			serial_printf(screen, "f:%lu,t:%d\n", libk_get_fps(), (uint32_t)t); 
		}
		PT_YIELD(pt); */
	}
	
	PT_END(pt); 
}

int main(void){
	fst6_init(); 
	
	printf("SystemCoreClock: %d\n", (int)SystemCoreClock); 
	
	app.eeprom = fst6_get_storage_device(); 
	
	gui_init(fst6_get_screen_framebuffer_interface()); 
	
	gui = gui_get_data(); 
	
	//status = DEMO_STATUS_WR_CONFIG | DEMO_STATUS_RD_CONFIG; 
	/*
	// test config read/write (to eeprom)
	const char str[] = "Hello World!"; 
	uint8_t buf[13] = {0}; 
	printf("Writing string to config: %s\n", str); 
	fst6_write_config((const uint8_t*)str, sizeof(str)); 
	printf("Reading string from config: "); 
	fst6_read_config(buf, sizeof(str)); 
	printf("%s\n", buf); 
	*/
	
	printf("Running libk loop\n"); 
	
	libk_run(); 
}
