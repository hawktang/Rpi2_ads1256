/*
	Author: Jure Bartol
	Date: 07.05.2016
	TO-DO:
		- divide into files
		- scanSEChannelsContinuous()
		- scanDIFFChannelsContinuous()
		- conversion into volts
		- part 3: "high level" data acquisition functions
		- python wrapper
*/

/*
	Structure:
		0. enumerations (adresses, rates etc.)
		1. functions for serial interface connection (programming pins DRDY, CS, sending, 
		receiving bytes over SPI)
		2. driver for ads1256 chip (controlling multiplexer, PGA, filter)
		3. "high level" data acquisition functions
*/

// Build example: gcc new_ads1256.c -std=c99 -o ads1256 -lbcm2835

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <bcm2835.h>  

/*
	***************************
	** PART 0 - enumerations **
	***************************
	Enumerations:
		- PGA   - programmable gain amplifier (PGA) settings
		- DRATE - data rate of programmable filter settings
		- REG   - register control adresses
		- CMD   - commands for controlling operation of ADS1256
		- AIN   - input analog channels
*/

// Set unsigned integer types.
#define uint8_t  unsigned char  	// 1 byte
#define uint16_t unsigned short 	// 2 bytes
#define uint32_t unsigned long  	// 4 bytes
//#define uint64_t unsigned long long // 8 bytes

//  Set the Programmable gain amplifier (PGA).
//	PGA Provides more resolution when measuring smaller input signals.
//	Set the PGA to the highest possible setting.
enum
{
	PGA_GAIN1	= 0, // Input voltage range: +- 5 V
	PGA_GAIN2	= 1, // Input voltage range: +- 2.5 V
	PGA_GAIN4	= 2, // Input voltage range: +- 1.25 V
	PGA_GAIN8	= 3, // Input voltage range: +- 0.625 V
	PGA_GAIN16	= 4, // Input voltage range: +- 0.3125 V
	PGA_GAIN32	= 5, // Input voltage range: +- 0.15625 V
	PGA_GAIN64	= 6  // Input voltage range: +- 0.078125 V
};

//  Set a data rate of a programmable filter (programmable averager).
//	Programmable from 30,000 to 2.5 samples per second (SPS).
//	Setting the data rate to high value results in smaller resolution of the data.
enum
{
	DRATE_30000 = 0xF0, 
	DRATE_15000 = 0xE0,
	DRATE_7500  = 0xD0,
	DRATE_3750  = 0xC0,
	DRATE_2000  = 0xB0,
	DRATE_1000  = 0xA1,
	DRATE_500   = 0x92,
	DRATE_100   = 0x82,
	DRATE_60    = 0x72,
	DRATE_50    = 0x63,
	DRATE_30    = 0x53,
	DRATE_25    = 0x43,
	DRATE_15    = 0x33,
	DRATE_10    = 0x20,
	DRATE_5     = 0x13,
	DRATE_2d5   = 0x03
};

//  Set of registers.
//	The operation of the ADS1256 is controlled through a set of registers. 
//	Collectively, the registers contain all the information needed to configure 
//	data rate, multiplexer settings, PGA setting, calibration, etc.
enum
{
	REG_STATUS = 0,	 // Register adress: 00h, Reset value: x1H
	REG_MUX    = 1,  // Register adress: 01h, Reset value: 01H
	REG_ADCON  = 2,  // Register adress: 02h, Reset value: 20H
	REG_DRATE  = 3,  // Register adress: 03h, Reset value: F0H
	REG_IO     = 4,  // Register adress: 04h, Reset value: E0H
	REG_OFC0   = 5,  // Register adress: 05h, Reset value: xxH
	REG_OFC1   = 6,  // Register adress: 06h, Reset value: xxH
	REG_OFC2   = 7,  // Register adress: 07h, Reset value: xxH
	REG_FSC0   = 8,  // Register adress: 08h, Reset value: xxH
	REG_FSC1   = 9,  // Register adress: 09h, Reset value: xxH
	REG_FSC2   = 10, // Register adress: 0Ah, Reset value: xxH
};

//  This commands control the operation of the ADS1256. 
//	All of the commands are stand-alone except for the register reads and writes 
//	(RREG, WREG) which require a second command byte plus data.
//	CS must stay low (CS_0()) during the entire command sequence.
enum
{
	CMD_WAKEUP   = 0x00, // Completes SYNC and Exits Standby Mode
	CMD_RDATA    = 0x01, // Read Data
	CMD_RDATAC   = 0x03, // Read Data Continuously
	CMD_SDATAC   = 0x0F, // Stop Read Data Continuously
	CMD_RREG     = 0x10, // Read from REG - 1st command byte: 0001rrrr 
						 //					2nd command byte: 0000nnnn
	CMD_WREG     = 0x50, // Write to REG  - 1st command byte: 0001rrrr
						 //					2nd command byte: 0000nnnn
						 // r = starting reg address, n = number of reg addresses
	CMD_SELFCAL  = 0xF0, // Offset and Gain Self-Calibration
	CMD_SELFOCAL = 0xF1, // Offset Self-Calibration
	CMD_SELFGCAL = 0xF2, // Gain Self-Calibration
	CMD_SYSOCAL  = 0xF3, // System Offset Calibration
	CMD_SYSGCAL  = 0xF4, // System Gain Calibration
	CMD_SYNC     = 0xFC, // Synchronize the A/D Conversion
	CMD_STANDBY  = 0xFD, // Begin Standby Mode
	CMD_RESET    = 0xFE, // Reset to Power-Up Values
};

// Input analog channels.
enum
{
	AIN0   = 0, //Binary value: 0000 0000
	AIN1   = 1, //Binary value: 0000 0001
	AIN2   = 2, //Binary value: 0000 0010
	AIN3   = 3, //Binary value: 0000 0011
	AIN4   = 4, //Binary value: 0000 0100
	AIN5   = 5, //Binary value: 0000 0101
	AIN6   = 6, //Binary value: 0000 0110
	AIN7   = 7, //Binary value: 0000 0111
	AINCOM = 8  //Binary value: 0000 1000
};

// Boolean values.
typedef enum
{
	False = 0,
	True  = 1,
} bool;


/*
	*******************************
	** PART 1 - serial interface **
	*******************************
	Functions:
		- CS_1()
		- CS_0()
		- RST_1()
		- RST_0()
		- DRDY_LOW()
		- delayus()
		- send8bit()
		- recieve8bit()
		- waitDRDY()
		- initializeSPI()
		- endSPI()
*/

// DRDY (ads1256 data ready output) - used as status signal to indicate when 
// conversion data is ready to be read.  
// Low  - new data avaliable, high - 24 bits are read or new data is being updated.
#define  DRDY		RPI_GPIO_P1_11
// RST (ADS1256 reset output)
#define  RST 		RPI_GPIO_P1_12
// SPICS (ADS1256 chip select) - allows individual selection of a ADS1256 device 
// when multiple devices share the serial bus. 
// Low - for the duration of the serial communication, high - serial interface is reset 
// and DOUT enters high impedance state.
#define	 SPICS		RPI_GPIO_P1_15
// DIN (data input) - send data to ADS1256. When SCLK goes from low to high.
#define  DIN 		RPI_GPIO_P1_19
// DOUT (data output) - read data from ADS1256. When SCLK goes from high to low.
#define  DOUT 		RPI_GPIO_P1_21
// SCLK (serial clock) - used to clock data on DIN and DOUT pins into and out of ADS1256.
// If not using external clock, ignore it.
#define  SCLK 		RPI_GPIO_P1_23
// Set SPICS to high (DOUT goes high).
#define  CS_1()  	bcm2835_gpio_write(SPICS, HIGH)
// Set SPICS to low (for serial communication).
#define  CS_0()  	bcm2835_gpio_write(SPICS, LOW)
// Set RST to high.
#define  RST_1() 	bcm2835_gpio_write(RST, HIGH)
// Set RST to low.
#define  RST_0() 	bcm2835_gpio_write(RST, LOW)
// Returns True if DRDY is low.
#define  DRDY_LOW()	bcm2835_gpio_lev(DRDY)==0


// Delay in microseconds.
void delayus(uint64_t microseconds)
{
	bcm2835_delayMicroseconds(microseconds);
}

// Send 8 bit value over serial interface (SPI).
void send8bit(uint8_t data)
{
	bcm2835_spi_transfer(data);
}

// Recieve 8 bit value over serial interface (SPI).
uint8_t recieve8bit(void)
{
	uint8_t read = 0;
	read = bcm2835_spi_transfer(0xff);
	return read;
}

// Wait until DRDY is low.
void waitDRDY(void)
{
	while(!DRDY_LOW()){
		continue;
	}
}

// Initialize SPI, call every time at the start of the program.
// Returns 1 if succesfull!
uint8_t initializeSPI()
{
	if (!bcm2835_init())
	    return -1;
	bcm2835_spi_begin();
	bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_LSBFIRST);
	bcm2835_spi_setDataMode(BCM2835_SPI_MODE1);
	// Spi clock divider: 250Mhz / 256 = 0.97 Mhz ~ between 4 to 10 * 1/freq.clkin.
	// Divider 128 is already more than 4 * 1/freq.clckin so it is not apropriate for usage.
	bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_256);
	bcm2835_gpio_fsel(SPICS, BCM2835_GPIO_FSEL_OUTP); // Set SPICS pin to output
	bcm2835_gpio_write(SPICS, HIGH);
	bcm2835_gpio_fsel(DRDY, BCM2835_GPIO_FSEL_INPT);  // Set DRDY pin to input
	bcm2835_gpio_set_pud(DRDY, BCM2835_GPIO_PUD_UP); 
	return 1;
}

// End SPI, call every time at the end of the program.
void endSPI()
{
	bcm2835_spi_end();
	bcm2835_close();
}


/*
	*****************************
	** PART 2 - ads1256 driver **
	*****************************
	Functions:
		- readByteFromReg()
		- writeByteToReg()
		- writeCMD()
		- readChipID()
		- setSEChannel()
		- setDIFFChannel()
		- setPGA()
		- setDataRate()
		- readData()
		- getValSEChannel()
		- getValDIFFChannel()
		- scanSEChannels()
		- scanDIFFChannels()
		- scanSEChannelsContinuous()
		- scanDIFFChannelsContinuous()
*/

// Read 1 byte from register address registerID. 
// This could be modified to read any number of bytes from register!	
uint8_t readByteFromReg(uint8_t registerID)
{
	CS_0();
	send8bit(CMD_RREG | registerID); // 1st byte: address of the first register to read
	send8bit(0x00); 				 // 2nd byte: number of bytes to read = 1.
	
	delayus(7); 	// min delay: t6 = 50 * 1/freq.clkin = 50 * 1 / 7,68 Mhz = 6.5 micro sec
	uint8_t read = recieve8bit();
	CS_1();
	return read;
}

// Write value (1 byte) to register address registerID.
// This could be modified to write any number of bytes to register!
void writeByteToReg(uint8_t registerID, uint8_t value)
{
	CS_0();
	send8bit(CMD_WREG | registerID); // 1st byte: address of the first register to write
	send8bit(0x00); 				 // 2nd byte: number of bytes to write = 1.
	send8bit(value);				 // 3rd byte: value to write to register
	CS_1();
}

// Send standalone commands to register.
uint8_t writeCMD(uint8_t command)
{
	CS_0();
	send8bit(command);
	CS_1();
}

// Set the internal buffer (True - enable, False - disable).
uint8_t setBuffer(bool val)
{
	//00000010
	CS_0();
	send8bit(CMD_WREG | REG_STATUS);
	send8bit((0 << 3) | (1 << 2) | (val << 1));
	CS_1();
}

// Get data from STATUS register - chip ID information.
uint8_t readChipID(void)
{
	waitDRDY();
	uint8_t id = readByteFromReg(REG_STATUS);
	return (id >> 4); // Only bits 7,6,5,4 are the ones to read (only in REG_STATUS) - return shifted value!
}

// Write to MUX register - set channel to read from in single-ended mode.
// Bits 7,6,5,4 determine the positive input channel (AINp).
// Bits 3,2,1,0 determine the negative input channel (AINn).
void setSEChannel(uint8_t channel)
{
	writeByteToReg(REG_MUX, channel << 4 | 1 << 3); // xxxx1000 - AINp = channel, AINn = AINCOM
}

// Write to MUX register - set channel to read from in differential mode.
// Bits 7,6,5,4 determine the positive input channel (AINp).
// Bits 3,2,1,0 determine the negative input channel (AINn).
void setDIFFChannel(uint8_t positiveCh, uint8_t negativeCh)
{
	writeByteToReg(REG_MUX, positiveCh << 4 | negativeCh); // xxxx1000 - AINp = positiveCh, AINn = negativeCh
}

// Write to A/D control register - set programmable gain amplifier (PGA).
// CLKOUT and sensor detect options are turned off in this case.
void setPGA(uint8_t pga)
{
	writeByteToReg(REG_ADCON, pga); // 00000xxx -> xxx = pga 
}

// Write to A/D data rate register - set data rate.
void setDataRate(uint8_t drate)
{
	writeByteToReg(REG_DRATE, drate);
}

// Read 24 bit value from ADS1256. Issue this command after DRDY goes low to read s single
// conversion result. Allows reading data from multiple different channels and in 
// single-ended and differential analog input.
int32_t readData(void)
{
	uint32_t read = 0;
	uint8_t buffer[3];

	CS_0();
	send8bit(CMD_RDATA);
	delayus(7); // min delayus: t6 = 50 * 1/freq.clkin = 50 * 1 / 7,68 Mhz = 6.5 micro sec

	buffer[0] = recieve8bit();
	buffer[1] = recieve8bit();
	buffer[2] = recieve8bit(); //DRDY goes back high here

	// construct 24 bit value
	read =  ((uint32_t)buffer[0] << 16) & 0x00FF0000;
	read |= ((uint32_t)buffer[1] << 8);
	read |= buffer[2];
	if (read & 0x800000){
		read |= 0xFF000000;
	}

	CS_1();

	return (int32_t)read;
}

// Get one single-ended analog input value by issuing command to input multiplexer.
// It reads a value from previous conversion!
// DRDY needs to be low!
int32_t getValSEChannel(uint8_t channel)
{
	int32_t read = 0;
	setSEChannel(channel); // MUX command
	delayus(3); // min delay: t11 = 24 * 1 / 7,68 Mhz = 3,125 micro sec
	writeCMD(CMD_SYNC);    // SYNC command
	delayus(3);
	writeCMD(CMD_WAKEUP);  // WAKEUP command
	delayus(1); // min delay: t11 = 4 * 1 / 7,68 Mhz = 0,52 micro sec
	read = readData();
	return read;
}

// Get one differential analog input value by issuing command to input multiplexer.
// It reads a value from previous conversion!
// DRDY needs to be low!
int32_t getValDIFFChannel(uint8_t positiveCh, uint8_t negativeCh)
{
	int32_t read = 0;
	setDIFFChannel(positiveCh, negativeCh);
	delayus(3); // min delayus: t11 = 24 * 1 / 7,68 Mhz = 3,125 micro sec
	writeCMD(CMD_SYNC);
	delayus(3);
	writeCMD(CMD_WAKEUP);
	delayus(1); // min delayus: t11 = 4 * 1 / 7,68 Mhz = 0,52 micro sec
	read = readData();
	return read;
}

// Get one single-ended analog input value from input channels you set (min 1, max 8).
void scanSEChannels(uint8_t channels[], uint8_t numOfChannels, uint32_t *values)
{
	for (int i = 0; i < numOfChannels; ++i){
		waitDRDY();
		values[i] = getValSEChannel(channels[i]);
	}
}

// Get one differential analog input value from input channels you set (min 1, max 4).
void scanDIFFChannels(uint8_t positiveChs[], uint8_t negativeChs[], uint8_t numOfChannels, uint32_t *values)
{
	for (int i = 0; i < numOfChannels; ++i){
		waitDRDY();
		values[i] = getValDIFFChannel(positiveChs[i], negativeChs[i]);
	}
}

// Continuously acquire analog data from one single-ended analog input.
// Allows sampling of one single-ended input channel up to 30,000 SPS.
void scanSEChannelsContinuous(uint8_t channel, uint32_t *values)
{

}

// Continuously acquire analog data from one differential analog input.
// Allows sampling of one differential input channel up to 30,000 SPS.
void scanDIFFChannelsContinuous(uint8_t positiveCh, uint8_t negativeCh, uint32_t *values)
{

}

/*
	*******************************
	** PART 3 - data acquisition **
	*******************************
	Functions:
		- acquisition with flushing to file (longer periods)
		- acquisition without flushing to file (short time)
*/







// Example of a simple program.	
int main(int argc, char *argv[]){
	if (argc < 2)
	{
		printf("Usage: %s <number of measurements>\n", argv[0]);
	}

	// Initialization and AD configuration
	if (!initializeSPI()) return 1;
	setBuffer(False);
	setPGA(PGA_GAIN1);
	setDataRate(DRATE_30000);


	//Single-ended input channels
	clock_t start_SE, end_SE;
	int num_ch_SE = 4;
	//int num_ch = 1;
	int num_measure_SE = atoi(argv[1]);
	uint32_t values_SE [num_ch_SE];
	uint8_t  channels_SE [4] = {AIN0, AIN1, AIN2, AIN3};
	//uint8_t channels [1] = {AIN0};
	start_SE = clock();
	for (int i = 0; i < num_measure_SE; ++i)
	{
		scanSEChannels(channels_SE, num_ch_SE, values_SE);
		printf("%i ", i+1);
		for (int ch = 0; ch < num_ch_SE; ++ch)
		{
			printf("%i ", values_SE[ch]);
		}
		printf("\n");
	}
	end_SE = clock();
	

	//Differential input channels
	clock_t start_DIFF, end_DIFF;
	int num_ch_DIFF = 2;
	int num_measure_DIFF = atoi(argv[1]);
	uint32_t values_DIFF [num_ch_DIFF];
	uint8_t  posChannels [2] = {AIN0, AIN2};
	uint8_t  negChannels [2] = {AIN1, AIN3};

	start_DIFF = clock();
	for (int i = 0; i < num_measure_DIFF; ++i)
	{
		scanDIFFChannels(posChannels, negChannels, num_ch_DIFF, values_DIFF);
		printf("%i ", i+1);
		for (int ch = 0; ch < num_ch_DIFF; ++ch)
		{
			printf("%i ", values_DIFF[ch]);
		}
		printf("\n");
	}
	end_DIFF = clock();
	printf("Time for %i single-ended measurements on %i channels is %d microseconds.\n", num_measure_SE, num_ch_SE, end_SE - start_SE);
	printf("Time for %i differential measurements on %i channels is %d microseconds.\n", num_measure_DIFF, num_ch_DIFF, end_DIFF - start_DIFF);


	endSPI();
	return 0;
}
