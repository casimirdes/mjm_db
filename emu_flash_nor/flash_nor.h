/*
 * flash_nor.h
 *
 *  Created on: 30 de mar. de 2025
 *      Author: mella
 */

#ifndef SRC_EMU_FLASH_NOR_FLASH_NOR_H_
#define SRC_EMU_FLASH_NOR_FLASH_NOR_H_

int mem_init(void);
int cria_binario_teste(void);
int mem_erase_chip(void);

int mem_erase_sector(const uint32_t sector);

int mem_read_sector(const uint32_t sector, uint8_t *buff);
int mem_write_sector(const uint32_t sector, const uint8_t *buff);

uint8_t mem_read_uint8(const uint32_t endereco);
uint32_t mem_read_uint32(const uint32_t endereco);
int mem_read_buff(const uint32_t endereco, const uint16_t size, uint8_t *buff);

int mem_write_uint8(const uint32_t endereco, const uint8_t data);
int mem_write_uint32(const uint32_t endereco, const uint32_t data);
int mem_write_buff(const uint32_t endereco, const uint8_t *buff, const uint16_t size);


void print_mem_STATUS_SECTORS(void);


/*

int mem_write_page(const uint32_t endereco, const uint8_t *buff);

int mem_erase_sector_2(const uint32_t sector);

int mem_erase_end_sectors(const uint32_t end, const uint32_t sectors);
int mem_erase_end_blocks(const uint32_t end, const uint32_t blocks);
*/

#endif /* SRC_EMU_FLASH_NOR_FLASH_NOR_H_ */
