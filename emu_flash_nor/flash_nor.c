/*
 * flash_nor.c
 *
 *  Created on: 30 de mar. de 2025
 *      Author: mella
 */

// emulando uma memoria flash do tipo NOR de 4096 bytes de setor, estilo w25qxxx
// ponto fraco ou ineficiente é que estou sempre abrindo e fechando o arquivo é mais seguro mas......

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "flash_nor.h"


#define SIZE_MEM				8388608  // 8388608 = 8*1024*1024  8MB  16777216 = 16*1024*1024  16MB
#define SIZE_SECTOR				4096
#define SIZE_PAGE				256
#define SIZE_SECTORS_MODS		(SIZE_MEM/SIZE_SECTOR)

const char *pasta = "src/emu_flash_nor/";
const char *filename_flash = "flash_mem.bin";

static FILE *file_bin;
static char caminho[128]={0};
static uint8_t buf_sector_a[SIZE_SECTOR];
static uint8_t buf_sector_b[SIZE_SECTOR];
static uint8_t flag_file_ok = 0;


static uint16_t sectors_mods[SIZE_SECTORS_MODS]={0};
static uint16_t sectors_mods_error = 0;




void incrementa_STATUS_SECTORS(const uint32_t sector)
{
	if(sector<SIZE_SECTORS_MODS)
	{
		sectors_mods[sector]+=1;
	}
	else
	{
		sectors_mods_error+=1;
	}
}



void print_mem_STATUS_SECTORS(void)
{
	uint32_t totales=0;
	uint16_t j, i;
	char buff[128];

	// vamos fazer a leitura do que temos alocado em 'sectors_mods[]' e 'sectors_mods_error' tudo do tipo 'static uint16_t'

	i = sprintf(buff, "\n================================================\nprint_mem_STATUS_SECTORS:\n");
	printf(buff);

	for(j=0; j<SIZE_SECTORS_MODS; j+=1)
	{
		if(sectors_mods[j])
		{
			i = sprintf(buff, "setor[%04u]=%u\n", j, sectors_mods[j]);
			printf(buff);
		}
		totales += sectors_mods[j];
	}

	i = sprintf(buff, "total setores:%u sectors_mods_error:%u\n================================================\n", totales, sectors_mods_error);
	printf(buff);
}


int mem_init(void)
{
	uint32_t size=0;
	int erro=0;

	flag_file_ok = 0;
	sprintf(caminho, "%s%s", pasta, filename_flash);

	if((file_bin = fopen(caminho, "rb")) == NULL)  // "r+"
	{
		printf("Erro ao abrir arquivo:|%s|\n", filename_flash);
		erro = -1;
		goto deu_erro;
	}

	fseek(file_bin, 0, SEEK_END);
	size = ftell(file_bin);
	fclose(file_bin);

	if(size != SIZE_MEM)
	{
		printf("arquivo:|%s| tamanho diferente do definido:%u|%u\n", filename_flash, size, SIZE_MEM);
		erro = -2;
		goto deu_erro;
	}


	flag_file_ok = 1;

	deu_erro:

	printf("arquivo:|%s| TAMANHO MEM:%u bytes, caminho:|%s|\n", filename_flash, size, caminho);

	return erro;
}


int cria_binario_teste(void)
{
	uint32_t sectors=0, i, size=0;
	int erro=0;

	sprintf(caminho, "%s%s", pasta, filename_flash);

	if((file_bin = fopen(caminho, "wb")) == NULL)  // "r+"
	{
		printf("Erro ao abrir arquivo:|%s|\n", filename_flash);
		erro = -1;
		goto deu_erro;
	}

	fseek(file_bin, 0, SEEK_SET);
	erro = 0;
	sectors = SIZE_MEM/SIZE_SECTOR;
	memset(buf_sector_a, 0xff, SIZE_SECTOR);
	for(i=0; i<sectors; i++)
	{
		size += fwrite(buf_sector_a, SIZE_SECTOR, 1, file_bin);
	}

	printf("cria_binario_teste: erro:%i, %u bytes\n", erro, size);

	fseek(file_bin, 0, SEEK_END);
	size = ftell(file_bin);
	fclose(file_bin);

	if(size != SIZE_MEM)
	{
		printf("arquivo:|%s| tamanho diferente do definido:%u|%u\n", filename_flash, size, SIZE_MEM);
		erro = -2;
		goto deu_erro;
	}

	printf("erro:%i, arquivo:|%s| TAMANHO MEM:%u bytes, caminho:|%s|\n", erro, filename_flash, size, caminho);
	flag_file_ok = 1;

	deu_erro:

	return -1;  // erro;
}

int mem_erase_chip(void)
{
	uint32_t sectors=0, i, size=0;
	int erro = -1;

	if(flag_file_ok==0)
	{
		erro = -1;
		goto deu_erro;
	}

	if((file_bin = fopen(caminho, "r+b")) == NULL)  // "r+"
	{
		erro = -2;
		goto deu_erro;
	}

	fseek(file_bin, 0, SEEK_SET);
	erro = 0;
	sectors = SIZE_MEM/SIZE_SECTOR;
	memset(buf_sector_a, 0xff, SIZE_SECTOR);
	for(i=0; i<sectors; i++)
	{
		size += fwrite(buf_sector_a, SIZE_SECTOR, 1, file_bin);
	}

	fclose(file_bin);

	deu_erro:

	printf("mem_erase_chip: erro:%i, %u bytes, caminho:|%s|\n", erro, size, caminho);

	return erro;
}


int mem_erase_sector(const uint32_t sector)
{
	uint32_t endereco, cont_bytes;
	int erro=0;
	uint8_t buf_sector[SIZE_SECTOR];

	if(sector>=(SIZE_MEM/SIZE_SECTOR))
	{
		erro = -1;
		goto deu_erro;
	}

	if(flag_file_ok==0)
	{
		erro = -2;
		goto deu_erro;
	}

	if((file_bin = fopen(caminho, "r+b")) == NULL) // "r+"
	{
		erro = -3;
		goto deu_erro;
	}

	endereco = sector*SIZE_SECTOR;

	erro = fseek(file_bin, endereco, SEEK_SET);
	if(erro!=0)
	{
		erro = -4;
		goto deu_erro;
	}

	memset(buf_sector, 0xff, SIZE_SECTOR);
	//cont_bytes = fwrite(buf_sector, SIZE_SECTOR, 1, file_bin);
	cont_bytes = fwrite(buf_sector, 1, SIZE_SECTOR, file_bin);

	if(cont_bytes != SIZE_SECTOR)
	{
		erro = -5;
		flag_file_ok = 0;
	}


	fclose(file_bin);

	incrementa_STATUS_SECTORS(sector);

	deu_erro:

	if(erro!=0)
	{
		printf("mem_erase_sector: erro:%i, sector:%u, cont_bytes:%u\n", erro, sector, cont_bytes);
	}

	return erro;
}


int mem_read_sector(const uint32_t sector, uint8_t *buff)
{
	uint32_t endereco, cont_bytes;
	int erro=0;

	if(sector>=(SIZE_MEM/SIZE_SECTOR))
	{
		erro = -1;
		goto deu_erro;
	}

	if(flag_file_ok==0)
	{
		erro = -2;
		goto deu_erro;
	}

	if((file_bin = fopen(caminho, "rb")) == NULL)  // "r+"
	{
		erro = -3;
		goto deu_erro;
	}

	endereco = sector*SIZE_SECTOR;

	erro = fseek(file_bin, endereco, SEEK_SET);
	if(erro!=0)
	{
		erro = -4;
		goto deu_erro;
	}

	//cont_bytes = fread(buff, SIZE_SECTOR, 1, file_bin);
	cont_bytes = fread(buff, 1, SIZE_SECTOR, file_bin);
	if(cont_bytes != SIZE_SECTOR)
	{
		erro = -5;
		flag_file_ok = 0;
	}


	fclose(file_bin);

	deu_erro:

	if(erro!=0)
	{
		printf("mem_read_sector: erro:%i, sector:%u, cont_bytes:%u\n", erro, sector, cont_bytes);
	}

	return erro;
}

/*
static int mem_sector_empty(const uint32_t sector)
{
	int i;
	mem_read_sector(sector, buf_sector_a);  // faz backup do setor
	for(i=0; i<SIZE_SECTOR; i++)
	{
		if(buf_sector_a[i]!=255) break;
	}
	//if(i<4096) return 1;  // se der 0 a 4095 indica que nao temos os 4096 com 255 logo nao está limpo!!!
	//return 0;
	return i;
}
*/



int mem_write_sector(const uint32_t sector, const uint8_t *buff)
{
	// setor ja deve estar limpo/erase!!!!

	uint32_t endereco, cont_bytes;
	int erro=0;

	if(sector>=(SIZE_MEM/SIZE_SECTOR))
	{
		erro = -1;
		goto deu_erro;
	}

	if(flag_file_ok==0)
	{
		erro = -2;
		goto deu_erro;
	}

	if((file_bin = fopen(caminho, "r+b")) == NULL)  // "r+"
	{
		erro = -3;
		goto deu_erro;
	}

	endereco = sector*SIZE_SECTOR;

	erro = fseek(file_bin, endereco, SEEK_SET);
	if(erro!=0)
	{
		erro = -4;
		goto deu_erro;
	}

	//cont_bytes = fwrite(buff, SIZE_SECTOR, 1, file_bin);
	cont_bytes = fwrite(buff, 1, SIZE_SECTOR, file_bin);
	if(cont_bytes != SIZE_SECTOR)
	{
		erro = -5;
		flag_file_ok = 0;
	}


	fclose(file_bin);

	//printf("mem_write_sector: buff[0]:%u, sector:%u\n", buff[0], sector);

	deu_erro:

	if(erro!=0)
	{
		printf("mem_write_sector: erro:%i, sector:%u, cont_bytes:%u\n", erro, sector, cont_bytes);
	}

	return erro;
}



static int mem_read_buff_aux(const uint32_t endereco, const uint16_t size, uint8_t *buff)
{
	uint32_t cont_bytes;
	int erro=0;

	if(endereco>=SIZE_MEM)
	{
		erro = -1;
		goto deu_erro;
	}

	if(flag_file_ok==0)
	{
		erro = -2;
		goto deu_erro;
	}

	if((file_bin = fopen(caminho, "rb")) == NULL)  // "r"
	{
		erro = -3;
		goto deu_erro;
	}

	erro = fseek(file_bin, endereco, SEEK_SET);
	if(erro!=0)
	{
		erro = -4;
		goto deu_erro;
	}

	//cont_bytes = fread(buff, size, 1, file_bin);
	cont_bytes = fread(buff, 1, size, file_bin);
	if(cont_bytes != size)
	{
		erro = -5;
		flag_file_ok = 0;
	}

	fclose(file_bin);

	deu_erro:

	if(erro!=0)
	{
		printf("mem_read_buff_aux: erro:%i, endereco:%u, size:%u, cont_bytes:%u\n", erro, endereco, size, cont_bytes);
	}

	return 0;
}


uint8_t mem_read_uint8(const uint32_t endereco)
{
	int erro=0;
	uint8_t b=0;

	erro = mem_read_buff_aux(endereco, 1, &b);

	return b;
}

uint32_t mem_read_uint32(const uint32_t endereco)
{
	uint32_t b32=0;
	int erro=0;

	erro = mem_read_buff_aux(endereco, 4, (uint8_t *)&b32);

	return b32;
}

int mem_read_buff(const uint32_t endereco, const uint16_t size, uint8_t *buff)
{
	int erro=0;

	erro = mem_read_buff_aux(endereco, size, buff);

	return erro;
}




static void mem_write_buff_aux2(const uint32_t endereco, const uint16_t size, const uint8_t *buff)
{
	uint32_t setor_a, setor_b, end_a, end_b, offset_a, offset_b, offlen, datalen, datalen_a, datalen_b;

	// nao vamos verificar se o esquema está apagado, vamos apagar mesmo assim para gravar o novo...
	datalen = size;  // 4096 bytes o máximo!!!!!!!!!!!!
	if(datalen>SIZE_SECTOR) datalen = SIZE_SECTOR;  // gambi mas é improvável que isso aconteça!!!

	setor_a = endereco/SIZE_SECTOR;  // descobre o setor do endereço
	end_a = setor_a*SIZE_SECTOR;  // descobre endereço zero do setor
	setor_b = setor_a+1;  // descobre setor seguinte
	end_b = setor_b*SIZE_SECTOR;  // descobre o endereço zero do setor seguinte
	//resto = endereco%4096;  // descobre o resto
	offset_a = endereco-end_a;  // descobre offset do setor_a, posicao do setor_a que se encontra o endereço
	offlen = offset_a + datalen;  // soma com o tamanho da data a ser gravada
	if(offlen>SIZE_SECTOR)  // passa para próximo setor
	{
		offset_b = 0;  // sempre vai começa do zero, logo nao seria necessário essa variavel
		datalen_a = SIZE_SECTOR-offset_a;
		datalen_b = datalen - datalen_a;

		mem_read_sector(setor_a, buf_sector_a);  // faz backup do setor
		mem_read_sector(setor_b, buf_sector_b);  // faz backup do setor
		memcpy(&buf_sector_a[offset_a], buff, datalen_a);  // escreve a nova data
		memcpy(&buf_sector_b[offset_b], &buff[datalen_a], datalen_b);  // escreve a nova data
		mem_erase_sector(setor_a);  // limpa setor
		mem_erase_sector(setor_b);  // limpa setor
		mem_write_sector(setor_a, buf_sector_a);  // grava no setor
		mem_write_sector(setor_b, buf_sector_b);  // grava no setor
	}
	else  // tudo acontece em um único setor
	{

		//printf("buff[0]:%u\n", buff[0]);
		//datalen_a = datalen+offset_a;  // mesmo que offlen
		mem_read_sector(setor_a, buf_sector_a);  // faz backup do setor
		memcpy(&buf_sector_a[offset_a], buff, datalen);  // escreve a nova data
		mem_erase_sector(setor_a);  // limpa setor
		mem_write_sector(setor_a, buf_sector_a);  // grava no setor
	}
}


static void mem_write_buff_aux1(const uint32_t endereco, const uint16_t size, const uint8_t *buff)
{
	uint32_t packs_4k, resto, i, j, end_aux;  // se entrar novamente/recursivo nao altera valor...
	if(size>SIZE_SECTOR)
	{
		// recursivo.... corta em pacotes de 4096 e chama a mesma funcao...
		packs_4k = size/SIZE_SECTOR;
		resto = size%SIZE_SECTOR;
		end_aux = endereco;
		j=0;
		for(i=0; i<packs_4k; i++)
		{
			mem_write_buff_aux2(end_aux, SIZE_SECTOR, &buff[j]);  // ou chamar o 'buf_sector_aux[]'!!!!
			end_aux+=SIZE_SECTOR;
			j+=SIZE_SECTOR;
		}
		if(resto)
		{
			mem_write_buff_aux2(end_aux, resto, &buff[j]);
		}
	}
	else
	{
		mem_write_buff_aux2(endereco, size, buff);
	}
}






int mem_write_uint8(const uint32_t endereco, const uint8_t data)
{
	uint8_t b[4];
	b[0] = data;
	mem_write_buff_aux1(endereco, 1, b);

	return 0;
}


int mem_write_uint32(const uint32_t endereco, const uint32_t data)
{
	uint8_t b[4];
	memcpy(b, &data, 4);  // b[0]=LSB b[3]=MSB

	mem_write_buff_aux1(endereco, 4, b);

	return 0;
}


int mem_write_buff(const uint32_t endereco, const uint8_t *buff, const uint16_t size)
{
	mem_write_buff_aux1(endereco, size, buff);

	return 0;
}













