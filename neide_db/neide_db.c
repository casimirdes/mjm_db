/*
 * neide_db.c
 *
 *  Created on: 12 de abr. de 2025
 *      Author: mella
 */


#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


#include "../emu_flash_nor/flash_nor.h"  // "camada de baixo nível" da memória

#include "neide_db.h"

#define USO_DEBUG_LIB		1  // 0=microcontrolador, 1=PC
#define PRINT_DEBUG			1  // 1 = printa toda vida o debug

#define OFF_INIT_DATA_DB	12  // ((4B)status_id, (4B)check_ids, (4B)crc) offset que inicia a 'data' do item no banco...

// code check gravado em todos esquemas que envolve ou nao banco...
#define CODE_DB_CHECK		2254785413UL	// fixo e igual para todos!!!


#define MAX_DATA_DB			16384  	// tamanho máximo de cada pacote gravado no banco criado, melhor ser múltiplo de 4096
#define HEADER_DB			32  	// tamanho de offset de cabeçalho de configuracoes do banco...
#define SECTOR_SIZE_MEM		4096	// supondo uma memória flash NOR que é por setores fixos, aqui temos o tamanho em bytes do setor

#define VERSAO_NEIDE		0x4d4a4d02  // "MJM"2


enum e_config_neidedb
{
	eAutoLoop, 		// flag de configuração do auto_loop
	eCheckUpdID,	// flag validar ou nao 'check_ids' na funcao de update id
	eCheckAddID,	// flag add no lugar de inativo e válido nao muda id_cont
	eReservado3,
};



typedef struct
{
	// MANTER ESSA ORDEM!!!!! e até 32 'HEADER_DB' bytes!!!!
	uint32_t versao;			// 4 primeiros bytes sao a 'marca' = "MJM2"
	uint32_t max_packs;  		// maximo de pacotes armazenados
	uint32_t offset_pack;  		// offset de cada pacote no banco
	uint32_t code_db;  			// código para validar criacao do banco na posicao da memoria que será gravada
	uint32_t max_size;			// limite tamanho total que esse banco acupa em bytes
	uint32_t check_ids;			// esquema de validador que muda toda vez que o banco é criado/limpo onde cada id grava esse valor no pacote
	uint8_t configs[4];			// [0]=auto_loop, [1]=check_update_id
	uint32_t reservado3;
	// OBS: max 8 vars de 4 bytes!!!!!!!!!!!!
}header_db;


static uint8_t buf_db[MAX_DATA_DB];  // base no setor da memoria que vale 4096 bytes, 3 setores...
static uint8_t pack[HEADER_DB];

static uint32_t seed_prng = 667;  // pobre e sempre iguallll, pensar em algo melhor...
static uint8_t init_seed_prng = 0;


// copiado da lib do littlefs "lfs_crc()"
// Software CRC implementation with small lookup table
static uint32_t neidedb_crc(uint32_t crc, const uint8_t *buffer, const uint16_t size)
{
    static const uint32_t rtable[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };

    const uint8_t *data = buffer;

    for (size_t i = 0; i < size; i++)
    {
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 0)) & 0xf];
        crc = (crc >> 4) ^ rtable[(crc ^ (data[i] >> 4)) & 0xf];
    }

    return crc;
}

uint16_t neidedb_crc16(const uint8_t *buf, const uint16_t len)
{
	uint16_t crc = 0xffff, i;
	uint8_t j;

	for(i=0; i<len; i++)
	{
		crc ^= (uint16_t)buf[i];
		for(j=0; j<8; j++)
		{
			if (crc & 1)
			{
				crc = (crc >> 1) ^ 0xA001;
			}
			else
			{
				crc = (crc >> 1);
			}
		}
	}

	return crc;
}

// gambi do chatsgptéks de um "pseudorandom number generator"
uint32_t neidedb_prng(void)
{
    seed_prng = (seed_prng * 1103515245 + 12345) % 0xFFFFFFFF;
    //seed_prng = (seed_prng * 134775813 + 1) % 0xFFFFFFFF;
    //seed_prng = (seed_prng * 214013 + 2531011) % 0xFFFFFFFF;
    return seed_prng;
}



// valida as 2 variaveis que temos certeza que devem estar com esses valores define
static int _neidedb_check_db_init(const uint32_t end_db, header_db *s_neide)
{
	int erro;

	erro = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro==erNEIDEDB_OK)
	{
		memcpy(s_neide, pack, sizeof(*s_neide));

		if(s_neide->versao != VERSAO_NEIDE)
		{
			erro = erNEIDEDB_1;
		}
		else if(s_neide->code_db != CODE_DB_CHECK)
		{
			erro = erNEIDEDB_2;
		}
		// daria para validar o 's_neide->check_ids' geral para com o id=0??? pois tem que estar de acordo...
	}

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG _neidedb_check_db_init::: erro:%i\n", erro);
	}
#endif

	return erro;
}


static int _neidedb_statistics(const uint32_t end_db, header_db *s_neide, uint32_t *cont_ids_, uint32_t *id_libre_, uint32_t *id_cont_)
{
	uint32_t endereco, i, cont_ids=0, id_libre=0, status_id=0, id_cont=0, id_cont_maior=0, check_ids=0, id_cont_menor=0xffffffff, index_maior=0, index_menor=0;
	int erro;
	uint8_t valid=255, set_libre=0;
	uint8_t b[8];

	erro = _neidedb_check_db_init(end_db, s_neide);

	if(erro==erNEIDEDB_OK)
	{
		for(i=0; i<s_neide->max_packs; i++)
		{
			endereco = (i*s_neide->offset_pack+HEADER_DB);
			endereco += end_db;
			mem_read_buff(endereco, 8, b);
			memcpy(&status_id, b, 4);
			memcpy(&check_ids, &b[4], 4);
			//status_id = mem_read_uint32(endereco);

			if(s_neide->check_ids == check_ids)
			{
				valid = (uint8_t)status_id&0xff;
				id_cont = (status_id>>8)&0xffffff;

				if(valid<=1)  // só aceita 0 ou 1 caso contrário pode estar corrompido ou limpo 0xff
				{
					if(id_cont>id_cont_maior)
					{
						id_cont_maior = id_cont;
						index_maior = i;
					}

					if(id_cont<id_cont_menor)
					{
						id_cont_menor = id_cont;
						index_menor = i;
					}
				}

				if(valid==1)  // 'valid==1' cuidar pois pode haver 0 ou 255 indica que está vazio...
				{
					cont_ids+=1;
				}
				else
				{
					if(set_libre==0)
					{
						id_libre = i;  // encontrado o primeiro id_libre
						set_libre = 1;
					}
				}
				//printf("okkk id:%u, valid:%u, set_libre:%u, id_libre:%u\n", i, valid, set_libre, id_libre);
			}
			else
			{
				if(set_libre==0)
				{
					id_libre = i;  // encontrado o primeiro id_libre
					set_libre = 1;
				}
				//printf("erro id:%u\n", i);
			}
		}

		if(set_libre==0 && valid==255)
		{
			// tudos sao irregulares... vamos manter 0 em tudo...
			id_libre = cont_ids;
			//printf("nada1??? id_libre:%u\n", id_libre);
		}
		else if(set_libre==0)
		{
			// todos sao validos e ativos... logo o 'id_libre' será o proximo da contagem que parou
			id_libre = cont_ids;

			// e caso 'cont_ids == s_neide->max_packs' indicando que todos sao ativos e chegou no limite
			// entao é o brique de assumir que o novo 'id_libre' é o mais antigo dos 'id_cont'

			// se o "auto_loop" nao estiver ativado ele bloqueia...
			if(cont_ids == s_neide->max_packs)
			{
				if(s_neide->configs[eAutoLoop])
				{
					id_libre = index_menor;
				}
				else
				{
					erro = erNEIDEDB_LOT;
				}
			}

			//printf("nada2??? id_libre:%u, cont_ids:%u\n", id_libre, cont_ids);
		}
	}

	*cont_ids_ = cont_ids;
	*id_libre_ = id_libre;
	*id_cont_ = id_cont_maior;

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG _neidedb_statistics::: erro:%i, end_db:%u, cont_ids:%u, id_libre:%u, id_cont_maior:%u(%u), id_cont_menor:%u(%u)\n", erro, end_db, cont_ids, id_libre, id_cont_maior, index_maior, id_cont_menor, index_menor);
	}
#endif

	return erro;
}


int neidedb_init(void)
{
	return mem_init();
}


int neidedb_create(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint8_t auto_loop, const uint8_t check_update_id, const uint8_t check_add_id_inativo)
{
	int erro=erNEIDEDB_OK;
	header_db s_neide;

	memset(&s_neide, 0x00, sizeof(s_neide));

	s_neide.versao = VERSAO_NEIDE;
	s_neide.max_packs = max_packs;
	s_neide.offset_pack = offset_pack + OFF_INIT_DATA_DB;
	s_neide.code_db = CODE_DB_CHECK;
	s_neide.max_size = HEADER_DB + (s_neide.offset_pack * s_neide.max_packs);
	s_neide.configs[eAutoLoop] = auto_loop;
	s_neide.configs[eCheckUpdID] = check_update_id;
	s_neide.configs[eCheckAddID] = check_add_id_inativo;

	if(s_neide.offset_pack < MAX_DATA_DB)
	{
		//-------------------------------------------------------------------------------------------------------
		// bruxaria para gerar o 'check_ids' usando uma mistura de crc+seed_prng
		// vamos ver se ja nao existia algo nesse endereço da memória... analisando o id==0
		if(init_seed_prng==0)
		{
			// semente inicial iniciadaaaaaaaaaaaaaaaaa nesse instante existente
			mem_read_buff(end_db, (s_neide.offset_pack+HEADER_DB), buf_db);
			seed_prng = neidedb_crc(0xffffffff, buf_db, (s_neide.offset_pack+HEADER_DB));
			init_seed_prng=1;
			// vai servir de base para todos os próximo casos de uso quando formos criar um banco
		}

		s_neide.check_ids = neidedb_prng();
		//-------------------------------------------------------------------------------------------------------

		memcpy(pack, &s_neide, sizeof(s_neide));
		erro = mem_write_buff(end_db, pack, HEADER_DB);
	}
	else
	{
		// o pacote nao pode ser alocado no buffer geral da lib 'buf_db[MAX_DATA_DB]'
		erro = erNEIDEDB_0;
	}

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_create::: erro:%i\n", erro);
	}
#endif

	return erro;
}


int neidedb_check(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack)
{
	uint32_t max_size;
	int erro;
	header_db s_neide;

	erro = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro==erNEIDEDB_OK)
	{
		memcpy(&s_neide, pack, sizeof(s_neide));

		if(s_neide.versao != VERSAO_NEIDE)
		{
			erro = erNEIDEDB_3;
		}
		else if(s_neide.code_db != CODE_DB_CHECK)
		{
			erro = erNEIDEDB_4;
		}
		else if(max_packs!=0 && offset_pack!=0)
		{
			max_size = HEADER_DB + (offset_pack * max_packs);
			if(max_size > s_neide.max_size)  // > ou !=
			{
				erro = erNEIDEDB_5;
			}
		}
		else if(max_packs!=0 && s_neide.max_packs != max_packs)
		{
			erro = erNEIDEDB_6;
		}
		else if(offset_pack!=0 && s_neide.offset_pack != (offset_pack + OFF_INIT_DATA_DB))
		{
			erro = erNEIDEDB_7;
		}
	}

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_check::: erro:%i\n", erro);
	}
#endif

	return erro;
}


int neidedb_get_configs(const uint32_t end_db, const uint8_t tipo, uint32_t *config)
{
	uint32_t cont_ids=0, id_libre=0, id_cont=0;
	int erro;
	header_db s_neide;

	erro = _neidedb_statistics(end_db, &s_neide, &cont_ids, &id_libre, &id_cont);

	if(erro==erNEIDEDB_OK)
	{
		if(tipo==eCONT_IDS_DB)
		{
			*config = cont_ids;
		}
		else if(tipo==eID_LIBRE_DB)
		{
			*config = id_libre;
		}
		else if(tipo==eMAX_IDS_DB)
		{
			*config = s_neide.max_packs;
		}
		else if(tipo==eOFF_IDS_DB)
		{
			*config = s_neide.offset_pack-OFF_INIT_DATA_DB;  // remove parte "implicita" de 'OFF_INIT_DATA_DB'!!!
		}
		else if(tipo==eCODE_DB)
		{
			*config = s_neide.code_db;
		}
		else if(tipo==eMAX_SIZE_DB)
		{
			*config = s_neide.max_size;
		}
		else if(tipo==eCHECK_IDS_DB)
		{
			*config = s_neide.check_ids;
		}
		else if(tipo==eID_CONT_DB)
		{
			*config = id_cont;
		}
		else
		{
			erro = erNEIDEDB_8;
			*config = 0;
		}
	}

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_get_configs::: erro:%i\n", erro);
	}
#endif

	return erro;
}


int neidedb_get_valids(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids)
{
	uint32_t endereco, i, j=0, cont=0, status_id=0, check_ids=0;
	int erro;
	uint8_t valid=0;
	uint8_t b[8];
	header_db s_neide;

	erro = _neidedb_check_db_init(end_db, &s_neide);

	if(erro==erNEIDEDB_OK)
	{
		for(i=0; i<s_neide.max_packs; i++)
		{
			endereco = (i*s_neide.offset_pack+HEADER_DB);
			endereco += end_db;

			mem_read_buff(endereco, 8, b);
			memcpy(&status_id, b, 4);
			memcpy(&check_ids, &b[4], 4);

			if(s_neide.check_ids == check_ids)
			{
				// validar os dados via crc????

				valid = (uint8_t)status_id&0xff;

				//printf("valid:%u\n", valid);
				if(valid==1)  // 'valid==1' cuidar pois pode haver 0 ou 255 indica que está vazio...
				{
					cont+=1;
					valids[j]=i;
					j+=1;
				}
			}
		}
	}

	*cont_ids = cont;

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_get_valids::: erro:%i\n", erro);
	}
#endif

	return erro;
}




int neidedb_get_info(const uint32_t end_db, char *sms, const char *nome)  // debug...
{
	uint32_t cont_ids=0, id_libre=0, id_cont=0;
	int erro, i=0;
	header_db s_neide={0};

	erro = _neidedb_statistics(end_db, &s_neide, &cont_ids, &id_libre, &id_cont);

	i = sprintf(sms, "---------------------------------\nBANCO:%s, END:%u, SETOR:%u, VERSAO:%08x, erro:%i\n"
			"\tMAX_PACKS:%u, OFFSET_PACK:%u, CODE:%u, max_size:%u, check_ids:%u, configs:[%u, %u, %u, %u]\n"
			"\tcont:%u, libre:%u, id_cont:%u\n---------------------------------\n",
			nome, end_db, (end_db/4096), s_neide.versao, erro, s_neide.max_packs,
			s_neide.offset_pack, s_neide.code_db, s_neide.max_size, s_neide.check_ids,
			s_neide.configs[0], s_neide.configs[1], s_neide.configs[2], s_neide.configs[3],
			cont_ids, id_libre, id_cont);

	return i;
}


int neidedb_info_deep(const uint32_t end_db, const char *nome_banco)
{
	uint32_t endereco, i, cont_ids=0, id_libre=0, status_id=0, id_cont=0, id_cont_maior=0, check_ids=0, id_cont_menor=0xffffffff, index_maior=0, index_menor=0;
	int erro;
	uint8_t valid=255, set_libre=0;
	uint8_t b[8];
	header_db s_neide={0};

	erro = _neidedb_check_db_init(end_db, &s_neide);

	printf("neidedb_info_deep: BANCO:%s\n", nome_banco);
	printf("END:%u, SETOR:%u, VERSAO:%08x, MAX_PACKS:%u, OFFSET_PACK:%u, CODE:%u, max_size:%u, check_ids:%u, configs:[%u, %u, %u, %u] erro:%i\n",
			end_db, (end_db/4096), s_neide.versao, s_neide.max_packs,
			s_neide.offset_pack, s_neide.code_db, s_neide.max_size, s_neide.check_ids,
			s_neide.configs[0], s_neide.configs[1], s_neide.configs[2], s_neide.configs[3], erro);

	if(erro==erNEIDEDB_OK)
	{
		//printf("| i | endereco | status_id | check_ids | valid | id_cont |\n");
	    printf("| %-3s | %-8s | %-9s | %-12s | %-5s | %-7s |\n",
	           "i", "endereco", "status_id", "check_ids", "valid", "id_cont");
	    printf("|-----|----------|-----------|--------------|-------|---------|\n");
		for(i=0; i<s_neide.max_packs; i++)
		{
			endereco = (i*s_neide.offset_pack+HEADER_DB);
			endereco += end_db;
			mem_read_buff(endereco, 8, b);
			memcpy(&status_id, b, 4);
			memcpy(&check_ids, &b[4], 4);
			//status_id = mem_read_uint32(endereco);

			if(s_neide.check_ids == check_ids)
			{
				valid = (uint8_t)status_id&0xff;
				id_cont = (status_id>>8)&0xffffff;

				if(valid<=1)  // só aceita 0 ou 1 caso contrário pode estar corrompido ou limpo 0xff
				{
					if(id_cont>id_cont_maior)
					{
						id_cont_maior = id_cont;
						index_maior = i;
					}

					if(id_cont<id_cont_menor)
					{
						id_cont_menor = id_cont;
						index_menor = i;
					}
				}

				if(valid==1)  // 'valid==1' cuidar pois pode haver 0 ou 255 indica que está vazio...
				{
					cont_ids+=1;
				}
				else
				{
					if(set_libre==0)
					{
						id_libre = i;  // encontrado o primeiro id_libre
						set_libre = 1;
					}
				}
				//printf("okkk id:%u, valid:%u, set_libre:%u, id_libre:%u\n", i, valid, set_libre, id_libre);
			}
			else
			{
				if(set_libre==0)
				{
					id_libre = i;  // encontrado o primeiro id_libre
					set_libre = 1;
				}
				//printf("erro id:%u\n", i);
			}

			//printf("| %u | %u | %u | %u | %u | %u |\n", i, endereco, status_id, check_ids, valid, id_cont);
			printf("| %-3u | %-8u | %-9u | %-12u | %-5u | %-7u |\n", i, endereco, status_id, check_ids, valid, id_cont);
			printf("|-----|----------|-----------|--------------|-------|---------|\n");
		}

		if(set_libre==0 && valid==255)
		{
			// tudos sao irregulares... vamos manter 0 em tudo...
			id_libre = cont_ids;
			//printf("nada1??? id_libre:%u\n", id_libre);
		}
		else if(set_libre==0)
		{
			// todos sao validos e ativos... logo o 'id_libre' será o proximo da contagem que parou
			id_libre = cont_ids;

			// e caso 'cont_ids == s_neide->max_packs' indicando que todos sao ativos e chegou no limite
			// entao é o brique de assumir que o novo 'id_libre' é o mais antigo dos 'id_cont'

			// se o "auto_loop" nao estiver ativado ele bloqueia...
			if(cont_ids == s_neide.max_packs)
			{
				if(s_neide.configs[eAutoLoop])
				{
					id_libre = index_menor;
				}
				else
				{
					erro = erNEIDEDB_LOT;
				}
			}

			//printf("nada2??? id_libre:%u, cont_ids:%u\n", id_libre, cont_ids);
		}
	}


	printf("DEBUG _neidedb_statistics::: erro:%i, end_db:%u, cont_ids:%u, id_libre:%u, id_cont_maior:%u(%u), id_cont_menor:%u(%u)\n", erro, end_db, cont_ids, id_libre, id_cont_maior, index_maior, id_cont_menor, index_menor);

	return erro;
}




int neidedb_read(const uint32_t end_db, const uint32_t id, uint8_t *data)
{
	uint32_t endereco, crc1, crc2, check_ids=0, id_cont=0, status_id=0;
	int erro;
	uint8_t valid;
	header_db s_neide;

	erro = _neidedb_check_db_init(end_db, &s_neide);

	if(erro==erNEIDEDB_OK)
	{
		if(id>=s_neide.max_packs)
		{
			erro = erNEIDEDB_9;
			goto deu_erro;
		}
		endereco = id * s_neide.offset_pack + HEADER_DB;
		endereco += end_db;

		// isso ja foi verificado quando cria o banco mas vamos fazer pra garantir...
		if(s_neide.offset_pack < MAX_DATA_DB)
		{
			//printf("endereco read:%u\n", endereco);
			erro = mem_read_buff(endereco, s_neide.offset_pack, buf_db);  // aloca pacote completo em 'buf_db[]'
			crc1 = neidedb_crc(0xffffffff, &buf_db[OFF_INIT_DATA_DB], s_neide.offset_pack-OFF_INIT_DATA_DB);
			memcpy(&status_id, buf_db, 4);
			memcpy(&check_ids, &buf_db[4], 4);
			memcpy(&crc2, &buf_db[8], 4);

			if(check_ids == s_neide.check_ids)
			{
				if(crc1 == crc2)
				{
					//valid = buf_db[0];
					valid = (uint8_t)status_id&0xff;
					id_cont = (status_id>>8)&0xffffff;
					// id_cont = buf_db[1:5];  24bits
					// validar o 'valid'?????

					// se assume que '*data' consegue alocar o tamanho de 's_neide.offset_pack' normalmente
					memcpy(data, &buf_db[OFF_INIT_DATA_DB], (s_neide.offset_pack-OFF_INIT_DATA_DB));
				}
				else
				{
					erro=erNEIDEDB_10;
				}
			}
			else
			{
				erro=erNEIDEDB_11;
			}
		}
		else
		{
			erro=erNEIDEDB_12;
		}
	}

	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_read::: erro:%i, end_db:%u, id:%u, valid:%u, id_cont:%u\n", erro, end_db, id, valid, id_cont);
	}
#endif

	return erro;
}




int neidedb_add(const uint32_t end_db, const uint8_t *data)
{
	uint32_t endereco=0, crc, cont_ids=0, id_libre=0, status_id=0, id_cont=0, check_ids=0;
	int erro;
	uint8_t b[8];
	header_db s_neide;

	erro = _neidedb_statistics(end_db, &s_neide, &cont_ids, &id_libre, &id_cont);

	if(erro==erNEIDEDB_OK)
	{
		// e se esse que vamos add em 'id_libre' for um invativo e com mesmo 's_neide.check_ids'??? nao eras de manter o 'id_cont'???
		// e se
		endereco = id_libre * s_neide.offset_pack + HEADER_DB;

		if(endereco > s_neide.max_size)
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro = erNEIDEDB_13;
			goto deu_erro;
		}

		// se desloca até offset do endereço do banco
		endereco += end_db;

		// análise do configs...
		mem_read_buff(endereco, 8, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);

		if(s_neide.configs[eCheckAddID]==1 && check_ids == s_neide.check_ids)
		{
			// deixar ativo (ou mesmo se estiver ja estiver ativo)
			status_id |= 0x01;
		}
		else
		{
			// segue fluxo como sempre foi... independe de nada... vai incrementar 'id_cont'
			id_cont += 1;  // auto incrementa...
			status_id = id_cont&0xffffff;
			status_id <<= 8;
			status_id |= 1;  // indica id ativo
		}

		// status_id
		memcpy(buf_db, &status_id, 4);

		// check_ids
		memcpy(&buf_db[4], &s_neide.check_ids, 4);

		// calcula crc
		crc = neidedb_crc(0xffffffff, data, s_neide.offset_pack-OFF_INIT_DATA_DB);
		memcpy(&buf_db[8], &crc, 4);

		// aloca a data pack
		memcpy(&buf_db[OFF_INIT_DATA_DB], data, s_neide.offset_pack-OFF_INIT_DATA_DB);



		//printf("endereco write:%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_neide.offset_pack);
		erro = mem_write_buff(endereco, buf_db, s_neide.offset_pack);

		/*
		// valida gravacao???
		memset(buf_db, 0x00, s_neide.offset_pack);
		mem_read_buff(endereco, s_neide.offset_pack, buf_db);
		printf("endereco read :%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_neide.offset_pack);
		*/
	}

	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_add::: erro:%i, end_db:%u, id:%u, status_id:%u, id_cont:%u\n", erro, end_db, id_libre, status_id, id_cont);
	}
#endif

	return erro;
}



int neidedb_update(const uint32_t end_db, const uint32_t id, uint8_t *data)
{
	uint32_t endereco=0, crc, status_id=0, check_ids=0, cont_ids=0, id_libre=0, id_cont=0;
	int erro;
	uint8_t b[8];
	header_db s_neide;

	//erro = _neidedb_check_db_init(end_db, &s_neide);
	erro = _neidedb_statistics(end_db, &s_neide, &cont_ids, &id_libre, &id_cont);

	if(erro==0)
	{
		if(id>=s_neide.max_packs)
		{
			erro = erNEIDEDB_16;
			goto deu_erro;
		}

		endereco = id * s_neide.offset_pack + HEADER_DB;

		if(endereco > s_neide.max_size)
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro = erNEIDEDB_17;
			goto deu_erro;
		}

		// se desloca até offset do endereço do banco
		endereco += end_db;

		mem_read_buff(endereco, 8, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);

		if(s_neide.configs[eCheckUpdID]==1 || check_ids == s_neide.check_ids)
		{
			if(s_neide.configs[eCheckUpdID]==1)
			{
				// vamos atualizar um antigo que pode ser lixo antigo de outro banco ou devido ter limpado o banco
				id_cont += 1;  // auto incrementa...
				status_id = id_cont&0xffffff;
				status_id <<= 8;
				status_id |= 1;  // indica id ativo
			}
			else
			{
				// deixar ativo (e se estiver desativado????)
				status_id |= 0x01;
			}

			// status_id
			memcpy(buf_db, &status_id, 4);

			// check_ids (vai manter o mesmo caso seja ok)
			memcpy(&buf_db[4], &s_neide.check_ids, 4);

			// calcula crc
			crc = neidedb_crc(0xffffffff, data, s_neide.offset_pack-OFF_INIT_DATA_DB);
			memcpy(&buf_db[8], &crc, 4);

			// aloca a data pack
			memcpy(&buf_db[OFF_INIT_DATA_DB], data, s_neide.offset_pack-OFF_INIT_DATA_DB);

			//printf("endereco write:%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_neide.offset_pack);
			erro = mem_write_buff(endereco, buf_db, s_neide.offset_pack);

			/*
			// valida gravacao???
			memset(buf_db, 0x00, s_neide.offset_pack);
			mem_read_buff(endereco, s_neide.offset_pack, buf_db);
			printf("endereco read :%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_neide.offset_pack);
			*/
		}
		else
		{
			// quer atualizar um pacote que nao está ok com o atual banco...
			erro = erNEIDEDB_18;
		}
	}

	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_update::: erro:%i, end_db:%u, id:%u, status_id:%u, id_cont:%u\n", erro, end_db, id, status_id, id_cont);
	}
#endif

	return erro;
}



int neidedb_update_old(const uint32_t end_db, const uint32_t id, uint8_t *data)
{
	uint32_t endereco=0, crc, status_id=0, check_ids=0;
	int erro;
	uint8_t b[8];
	header_db s_neide;

	erro = _neidedb_check_db_init(end_db, &s_neide);

	if(erro==0)
	{
		if(id>=s_neide.max_packs)
		{
			erro = erNEIDEDB_16;
			goto deu_erro;
		}

		endereco = id * s_neide.offset_pack + HEADER_DB;

		if(endereco > s_neide.max_size)
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro = erNEIDEDB_17;
			goto deu_erro;
		}

		// se desloca até offset do endereço do banco
		endereco += end_db;

		mem_read_buff(endereco, 8, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);

		if(s_neide.configs[eCheckUpdID]==1 || check_ids == s_neide.check_ids)
		{
			// deixar ativo (e se estiver desativado????)
			status_id |= 0x01;

			// status_id
			memcpy(buf_db, &status_id, 4);

			// check_ids (vai manter o mesmo caso seja ok)
			memcpy(&buf_db[4], &s_neide.check_ids, 4);

			// calcula crc
			crc = neidedb_crc(0xffffffff, data, s_neide.offset_pack-OFF_INIT_DATA_DB);
			memcpy(&buf_db[8], &crc, 4);

			// aloca a data pack
			memcpy(&buf_db[OFF_INIT_DATA_DB], data, s_neide.offset_pack-OFF_INIT_DATA_DB);

			//printf("endereco write:%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_neide.offset_pack);
			erro = mem_write_buff(endereco, buf_db, s_neide.offset_pack);

			/*
			// valida gravacao???
			memset(buf_db, 0x00, s_neide.offset_pack);
			mem_read_buff(endereco, s_neide.offset_pack, buf_db);
			printf("endereco read :%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_neide.offset_pack);
			*/
		}
		else
		{
			// quer atualizar um pacote que nao está ok com o atual banco...
			erro = erNEIDEDB_18;
		}
	}

	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_update::: erro:%i, end_db:%u, id:%u, status_id:%u\n", erro, end_db, id, status_id);
	}
#endif

	return erro;
}


int neidedb_del(const uint32_t end_db, const uint32_t id)
{
	uint32_t endereco=0, status_id, check_ids=0;
	int erro;
	uint8_t b[8];
	header_db s_neide;

	erro = _neidedb_check_db_init(end_db, &s_neide);

	if(erro==0)
	{
		if(id>=s_neide.max_packs)
		{
			erro = erNEIDEDB_14;
			goto deu_erro;
		}

		endereco = id * s_neide.offset_pack + HEADER_DB;

		if(endereco > s_neide.max_size)
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro = erNEIDEDB_15;
			goto deu_erro;
		}


		// se desloca até offset do endereço do banco
		endereco += end_db;

		// validar o pacote com check_ids e/ou crc??????
		mem_read_buff(endereco, 8, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);

		if(check_ids == s_neide.check_ids)
		{
			// faz parte do atual banco...

			if((uint8_t)status_id == 0)
			{
				erro = erNEIDEDB_DEL;  // quer deletar e ja está deletado (inativar)
				goto deu_erro;
			}

			// deixar inativo
			status_id &= 0xffffff00;

			// status_id
			memcpy(buf_db, &status_id, 4);

			//printf("endereco write:%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_neide.offset_pack);
			erro = mem_write_buff(endereco, buf_db, 4);
		}
		// caso 'check_ids != s_neide.check_ids' só ignora e segue o baile, nada de erros (temos lixos antigos)
	}

	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erNEIDEDB_OK)
	{
		printf("DEBUG neidedb_del::: erro:%i, end_db:%u, id:%u, status_id:%u\n", erro, end_db, id, status_id);
	}
#endif

	return erro;
}

