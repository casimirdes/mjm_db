/*
 * gilson_db.c
 *
 *  Created on: 2 de abr. de 2025
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



#define TIPO_DEVICE			1  // 0=microcontrolador, 1=PC
#define USO_DEBUG_LIB		0  // 0=desativado, 1=ativado
#define PRINT_DEBUG			0  // 1 = printa toda vida o debug




// aqui customiza conforme o periférico se é microcntrolador, um PC...
// se tem que add uma lib específica...

#if (TIPO_DEVICE==0)

...

#else


#include "../emu_flash_nor/flash_nor.h"  // "camada de baixo nível" da memória
#include "../gilson_c/gilson.h"

#endif  // #if (TIPO_DEVICE==1)


#include "gilson_db.h"


#define _bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define _bitSet(value, bit) ((value) |= (1UL << (bit)))
#define _bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define _bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))


#define LIMIT_KEYS			256  	// esse valor é duplicado "tipo1"+"tipo2" uint8_t (limite de 256 chaves de cada tabela/banco)
#define HEADER_DB			64  	// 4+60 tamanho de offset de cabeçalho de configuracoes do banco...
#define SECTOR_SIZE_MEM		4096	// supondo uma memória flash NOR que é por setores fixos, aqui temos o tamanho em bytes do setor

#define VERSAO_GILSONDB		0x4d4a4d64  // "MJM"64



typedef struct
{
	uint32_t versao;			// 4 primeiros bytes sao a 'marca' = "MJM2"
	uint32_t max_packs;  		// maximo de pacotes armazenados
	uint32_t code_db;  			// código para validar criacao do banco na posicao da memoria que será gravada
	uint32_t check_ids;			// esquema de validador que muda toda vez que o banco é criado/limpo onde cada id grava esse valor no pacote
	uint32_t size_max_pack;		// tamanho do pacote 'data' (no pior caso) e soma o 'OFF_PACK_GILSON_DB'
	uint32_t size_max_tot;		// tamanho maximo do pacote, caso o usuário entre com um valor especifico

	uint32_t configs_bits;		// ver 'e_config_gilsondb'

	uint16_t size_header;     	// tamanho do pacote header, vai ser dinamico agora... (da pra ser um uint16_t)

	uint16_t max_keys;			// total de chaves do gilson, até 256

	uint8_t multi_map;  		// mantem 1 para modo padrão, porem para modo de multi bancos no mesmo endereço, deve ser >1 e usar as funções 'map' específicas

	// OBS: max 64 bytes liquidos!!!!!!!!!!!!
}header_db;





static uint8_t buf_db[SECTOR_SIZE_MEM];  // base no setor da memoria que vale 4096 bytes, 3 setores...
static uint32_t seed_prng = 667;  // pobre e sempre iguallll, pensar em algo melhor...
static uint8_t init_seed_prng = 0;

static header_db s_gilsondb;  // para uso somente quando estamos criando um banco
static uint8_t flag_s_gilsondb_ativo = 0;
static int32_t erro_global = erGILSONDB_OK;

// copiado da lib do littlefs "lfs_crc()"
// Software CRC implementation with small lookup table
static uint32_t gilsondb_crc(uint32_t crc, const uint8_t *buffer, const uint16_t size)
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

static uint16_t gilsondb_crc16(const uint8_t *buf, const uint16_t len)
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
static uint32_t gilsondb_prng(void)
{
    seed_prng = (seed_prng * 1103515245 + 12345) % 0xFFFFFFFF;
    //seed_prng = (seed_prng * 134775813 + 1) % 0xFFFFFFFF;
    //seed_prng = (seed_prng * 214013 + 2531011) % 0xFFFFFFFF;
    return seed_prng;
}


static void update_erro_global(int32_t erro)
{
	if(erro!=erGILSONDB_OCUPADO)
	{
		erro_global = erro;
	}
}



static int32_t _encode_header_db(header_db *data, uint8_t *pack, const uint16_t size)
{
	uint32_t crc=0;
	int32_t erro=0, pos_bytes = 0;

    erro = gilson_encode_init(GSON_MODO_ZIP, pack, size);
    erro = gilson_encode(0, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->versao);
    erro = gilson_encode(1, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->max_packs);
    erro = gilson_encode(2, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->code_db);
    erro = gilson_encode(3, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->check_ids);
    erro = gilson_encode(4, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->size_max_pack);
    erro = gilson_encode(5, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->size_max_tot);
    erro = gilson_encode(6, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->configs_bits);
    erro = gilson_encode(7, GSON_SINGLE, GSON_tUINT16, CAST_GIL data->size_header);
    erro = gilson_encode(8, GSON_SINGLE, GSON_tUINT16, CAST_GIL data->max_keys);
    erro = gilson_encode(8, GSON_SINGLE, GSON_tUINT8, CAST_GIL data->multi_map);
    pos_bytes = gilson_encode_end(&crc);  // criar outra que nao devolve o crc!!!!

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG _encode_header_db::: erro:%i, pos_bytes:%lu, crc:%lu\n", erro, pos_bytes, crc);
#else  // PC
		printf("DEBUG _encode_header_db::: erro:%i, pos_bytes:%u, crc:%u\n", erro, pos_bytes, crc);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)

	update_erro_global(erro);

    return erro;
}

static int32_t _decode_header_db(header_db *data, const uint8_t *pack)
{
	uint32_t crc=0;
	int32_t erro=0, pos_bytes = 0;
	uint8_t modo=0;

	memset(data, 0x00, sizeof(*data));

	erro = gilson_decode_init(pack, &modo);
    erro = gilson_decode(0, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->versao);
    erro = gilson_decode(1, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->max_packs);
    erro = gilson_decode(2, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->code_db);
    erro = gilson_decode(3, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->check_ids);
    erro = gilson_decode(4, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->size_max_pack);
    erro = gilson_decode(5, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->size_max_tot);
    erro = gilson_decode(6, GSON_SINGLE, GSON_tUINT32, CAST_GIL data->configs_bits);
    erro = gilson_decode(7, GSON_SINGLE, GSON_tUINT16, CAST_GIL data->size_header);
    erro = gilson_decode(8, GSON_SINGLE, GSON_tUINT16, CAST_GIL data->max_keys);
    erro = gilson_decode(8, GSON_SINGLE, GSON_tUINT8, CAST_GIL data->multi_map);
	pos_bytes = gilson_decode_end(&crc);

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK || pos_bytes<0)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG _decode_header_db::: erro:%i, pos_bytes:%lu, crc:%lu\n", erro, pos_bytes, crc);
#else  // PC
		printf("DEBUG _decode_header_db::: erro:%i, pos_bytes:%u, crc:%u\n", erro, pos_bytes, crc);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)

	update_erro_global(erro);

	return erro;
}

static int32_t _get_header_db(const uint32_t end_db, header_db *s_gdb, uint32_t *crc_)
{
	uint32_t crc=0;
	int32_t erro=0;

	erro = mem_read_buff(end_db, HEADER_DB, buf_db);
	if(erro==0)
	{
		memcpy(&crc, buf_db, 4);
		erro = _decode_header_db(s_gdb, &buf_db[4]);
	}

	*crc_ = crc;
	return erro;
}

// valida as 2 variaveis que temos certeza que devem estar com esses valores define
static int32_t _gilsondb_check_db_init(const uint32_t end_db, header_db *s_gdb)
{
	uint32_t crc1=0, crc2=0;
	int32_t erro;

	if(flag_s_gilsondb_ativo==0)
	{
		//erro = mem_read_buff(end_db, HEADER_DB, buf_db);
		erro = _get_header_db(end_db, s_gdb, &crc1);

		if(erro==erGILSONDB_OK)
		{
			//memcpy(&crc1, buf_db, 4);
			//memcpy(s_gdb, &buf_db[4], sizeof(*s_gdb));

			if(s_gdb->versao != VERSAO_GILSONDB)
			{
				erro = erGILSONDB_1;
				goto deu_erro;
			}
			else if(s_gdb->size_header>SECTOR_SIZE_MEM)
			{
				erro = erGILSONDB_18;
				goto deu_erro;
			}

			// vamos ler pacote do header total
			erro = mem_read_buff(end_db+4, (s_gdb->size_header - 4), buf_db);  // le o restante do header completo!

			// valida o crc do size_header
			crc2 = gilsondb_crc(0xffffffff, buf_db, (s_gdb->size_header - 4));
			if(crc1 != crc2)
			{
				erro = erGILSONDB_0;
				goto deu_erro;
			}
			/*
			else if(s_gdb->versao != VERSAO_GILSONDB)
			{
				erro = erGILSONDB_1;
			}
			else if(s_gdb->code_db != codedb)
			{
				erro = erGILSONDB_2;
			}
			*/
			// daria para validar o 's_neide->check_ids' geral para com o id=0??? pois tem que estar de acordo...
		}
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}

	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG _gilsondb_check_db_init::: erro:%i, end_db:%lu(%lu)\n", erro, end_db, (end_db/4096));
#else  // PC
		printf("DEBUG _gilsondb_check_db_init::: erro:%i, end_db:%u(%u)\n", erro, end_db, (end_db/4096));
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)

	update_erro_global(erro);
	return erro;
}


static int32_t _gilsondb_statistics(const uint32_t end_db, header_db *s_gdb, uint32_t *cont_ids_, uint32_t *id_libre_, uint32_t *id_cont_, uint32_t *size_bytes_)
{
	uint32_t endereco=0, i, cont_ids=0, id_libre=0, status_id=0, id_cont=0, id_cont_maior=0, check_ids=0, id_cont_menor=0xffffffff, index_maior=0, index_menor=0, len_pacote=0, crc1=0, size_bytes=0;
	int32_t erro;
	uint8_t valid=255, set_libre=0, rOFF_PACK_GILSON_DB=0, i_banco=0;
	uint8_t b[OFF_PACK_GILSON_DB+1];

	// OBS: nao vamos validar crc de cada pacote

	erro = _gilsondb_check_db_init(end_db, s_gdb);

	if(erro==erGILSONDB_OK)
	{
		if(_bitRead(s_gdb->configs_bits, egFixedSize)==1)
		{
			if(s_gdb->multi_map>1)
			{
				rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB + 1;
			}
			else
			{
				rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB;
			}
			// aqui o que importa é descobrir o endereço de onde começa o proximo id libre e vamos alocar esse endereço em 'xxxxxx'
			endereco = s_gdb->size_header;  // 0 * s_gdb->size_max_pack + s_gdb->size_header
			endereco += end_db;
			for(i=0; i<s_gdb->max_packs; i++)
			{
				erro = mem_read_buff(endereco, rOFF_PACK_GILSON_DB, b);
				memcpy(&status_id, b, 4);
				memcpy(&check_ids, &b[4], 4);
				memcpy(&len_pacote, &b[8], 4);
				memcpy(&crc1, &b[12], 4);
				if(s_gdb->multi_map>1)
				{
					i_banco = b[16];
				}

				if(s_gdb->check_ids == check_ids)
				{
					valid = (uint8_t)status_id&0xff;
					id_cont = (status_id>>8)&0xffffff;

					// OBS: nesse modo, 'id_cont' segue o mesmo valor de 'cont_ids' pois só pode ir até o limite 'max_packs' e começa em 1

					if(valid==1)  // 'valid==1' cuidar pois pode haver 0 ou 255 indica que está vazio...
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

						cont_ids+=1;

						// é um dado valido entao avançamos
						len_pacote += rOFF_PACK_GILSON_DB;
						endereco += len_pacote;
						// resulta no endereço do proximo id
					}
					else
					{
						// tem problema, embora o 'check_ids' deu boa
						// vamos assumir que esse é um libre entao
						id_libre = i;  // encontrado o primeiro id_libre
						set_libre = 1;
						break;
					}
				}
				else
				{
					id_libre = i;  // encontrado o primeiro id_libre
					set_libre = 1;
					break;
				}
			}

			if(set_libre==0)
			{
				// todos ocupadosn ja... i==s_gdb->max_packs
				set_libre = 1;
				erro = erGILSONDB_LOT;
			}
			else
			{
				id_libre = endereco;  // gambi vai ser que nesse modo 'id_libre' vai receber o endereço para o proximo id libre!!!
			}


			//*cont_ids_ = cont_ids;
			//*id_libre_ = id_libre;
			//*id_cont_ = id_cont_maior;
		}
		else
		{
			for(i=0; i<s_gdb->max_packs; i++)
			{
				endereco = i * s_gdb->size_max_pack + s_gdb->size_header;
				endereco += end_db;

				mem_read_buff(endereco, OFF_PACK_GILSON_DB, b);
				memcpy(&status_id, b, 4);
				memcpy(&check_ids, &b[4], 4);

				if(s_gdb->check_ids == check_ids)
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
				if(cont_ids == s_gdb->max_packs)
				{
					if(_bitRead(s_gdb->configs_bits, egAutoLoop)==1)
					{
						id_libre = index_menor;
					}
					else
					{
						erro = erGILSONDB_LOT;
					}
				}

				//printf("nada2??? id_libre:%u, cont_ids:%u\n", id_libre, cont_ids);
			}
		}
	}

	// para modo 'egFixedSize' teremos contagem dos bytes usados
	// para modo sem 'egFixedSize' é o mesmo que percorrer tamanho maximo do banco
	size_bytes = endereco - end_db;  // total em bytes deslocado para fazer as leituras

	*cont_ids_ = cont_ids;
	*id_libre_ = id_libre;
	*id_cont_ = id_cont_maior;
	*size_bytes_ = size_bytes;

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG _gilsondb_statistics::: erro:%i, end_db:%lu(%lu), cont_ids:%lu, id_libre:%lu, id_cont_maior:%lu(%lu), id_cont_menor:%lu(%lu)\n", erro, end_db, (end_db/4096), cont_ids, id_libre, id_cont_maior, index_maior, id_cont_menor, index_menor);
#else  // PC
		printf("DEBUG _gilsondb_statistics::: erro:%i, end_db:%u(%u), cont_ids:%u, id_libre:%u, id_cont_maior:%u(%u), id_cont_menor:%u(%u)\n", erro, end_db, (end_db/4096), cont_ids, id_libre, id_cont_maior, index_maior, id_cont_menor, index_menor);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)

	update_erro_global(erro);
	return erro;
}




#if (USO_DEBUG_LIB==1)

static void _print_header_db(const header_db *s_gdb, const uint32_t end_db)
{
#if (TIPO_DEVICE==0)
	printf_DEBUG("DEBUG INFO STRUCT_DB::: end_db:%lu(%lu), versao:%08lx, max_packs:%lu, code_db:%lu, check_ids:%lu, size_max_pack:%lu, configs:0x%08lx, size_header:%lu, max_keys:%lu, erg:%i, ativo:%u\n",
			end_db, (end_db/4096),
			s_gdb->versao,
			s_gdb->max_packs,
			s_gdb->code_db,
			s_gdb->check_ids,
			s_gdb->size_max_pack,
			s_gdb->configs[0], s_gdb->configs[1], s_gdb->configs[2], s_gdb->configs[3],
			s_gdb->size_header,
			s_gdb->max_keys,
			erro_global,
			flag_s_gilsondb_ativo);
#else  // PC
	printf("DEBUG INFO STRUCT_DB::: end_db:%u(%u), versao:%08x, max_packs:%u, code_db:%u, check_ids:%u, size_max_pack:%u, configs:0x%08x, size_header:%u, max_keys:%u, erg:%i, ativo:%u\n",
			end_db, (end_db/4096),
			s_gdb->versao,
			s_gdb->max_packs,
			s_gdb->code_db,
			s_gdb->check_ids,
			s_gdb->size_max_pack,
			s_gdb->configs_bits,
			s_gdb->size_header,
			s_gdb->max_keys,
			erro_global,
			flag_s_gilsondb_ativo);
#endif  // #if (TIPO_DEVICE==1)


}
#endif  // #if (USO_DEBUG_LIB==1)



int32_t gilsondb_init(void)
{
#if (TIPO_DEVICE==0)
	return 0;
#else
	return mem_init();
#endif  // #if (TIPO_DEVICE==x)
}


// https://www.youtube.com/watch?v=rtGACXTp6mY
int32_t gilsondb_create_init(const uint32_t end_db, const uint32_t max_packs, const uint32_t codedb, const uint32_t max_bytes, const uint8_t *settings)
{
	int32_t erro=erGILSONDB_OK;
	uint8_t i;

	if(erro_global!=erGILSONDB_OCUPADO && flag_s_gilsondb_ativo==1)
	{
		erro_global = erGILSONDB_OK;
		flag_s_gilsondb_ativo = 0;
	}

	if(flag_s_gilsondb_ativo==0)
	{
		flag_s_gilsondb_ativo = 1;

		memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));
		memset(buf_db, 0x00, sizeof(buf_db));

		s_gilsondb.versao = VERSAO_GILSONDB;
		s_gilsondb.max_packs = max_packs;
		s_gilsondb.code_db = codedb;
		s_gilsondb.multi_map = 1;

		//um dos parameteos vai ser esquema de operar em saltos fixos de pior caso ou sempre dinamico
		//mas dai só permite ler e add toda vida até o fim do limite...
		//senao é possivel operar tipo banco que edita, exclui, add...
		for(i=0; i<egLENMAX; i++)
		{
			if(settings[i])
			{
				_bitSet(s_gilsondb.configs_bits, i);
			}
		}
		//s_gilsondb.configs_bits=0;
		//s_gilsondb.configs[0] = settings[0];
		//s_gilsondb.configs[1] = settings[1];
		//s_gilsondb.configs[2] = settings[2];
		//s_gilsondb.configs[3] = settings[3];

		if(_bitRead(s_gilsondb.configs_bits, egFixedSize)==1)
		{
			if(max_bytes==0)
			{
				erro = erGILSONDB_30;
				goto deu_erro;
			}
			s_gilsondb.size_max_tot = max_bytes;
		}
		// la em 'gilsondb_create_end()' vai atualizar com o valor final de 's_gilsondb.size_max_tot' para todos os modos!!!




		//-------------------------------------------------------------------------------------------------------
		// bruxaria para gerar o 'check_ids' usando uma mistura de crc+seed_prng
		// vamos ver se ja nao existia algo nesse endereço da memória... analisando o id==0
		if(init_seed_prng==0)
		{
			// semente inicial iniciadaaaaaaaaaaaaaaaaa nesse instante existente
			mem_read_buff(end_db, (666+HEADER_DB), buf_db);
			seed_prng = gilsondb_crc(0xffffffff, buf_db, (666+HEADER_DB));
			init_seed_prng=1;
			// vai servir de base para todos os próximo casos de uso quando formos criar um banco
		}
		s_gilsondb.check_ids = gilsondb_prng();
		//-------------------------------------------------------------------------------------------------------

		s_gilsondb.size_header = HEADER_DB;  // crc + header, começa assim e vai incrementando a cada chamada de 'gilsondb_create_add()'

		s_gilsondb.size_max_pack = OFF_PACK_GILSON_DB;  // pois todos pacote independente de quantos bytes terá (se dinamico) sempre vai ter 'OFF_PACK_GILSON_DB' no inicio
		s_gilsondb.size_max_pack += 1;  // é o offset de indentificação do pacote no modo 'GSON_MODO_ZIP' o chamado 'OFFSET_MODO_ZIP'

		/*
		//s_gilsondb.offset_pack = offset_pack + OFF_INIT_DATA_DB;
		//s_gilsondb.max_size = HEADER_DB + (s_gilsondb.offset_pack * s_gilsondb.max_packs);
		//s_gilson.configs[eAutoLoop] = settings[0];  // auto_loop;
		//s_gilson.configs[eCheckUpdID] = settings[1];  // check_update_id;

		if(1)  // s_gilsondb.offset_pack < MAX_DATA_DB
		{



			//erro = mem_write_buff(end_db, buf_db, HEADER_DB);
		}
		else
		{
			// o pacote nao pode ser alocado no buffer geral da lib 'buf_db[MAX_DATA_DB]'
			erro = erGILSONDB_0;
		}
		*/
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}

	deu_erro:

#if (USO_DEBUG_LIB==1)
	_print_header_db(&s_gilsondb, end_db);
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_create_init::: erro:%i, end_db:%lu(%lu), erro_global:%i, ativo:%u\n", erro, end_db, (end_db/4096), erro_global, flag_s_gilsondb_ativo);
#else  // PC
		printf("DEBUG gilsondb_create_init::: erro:%i, end_db:%u(%u), erro_global:%i, ativo:%u\n", erro, end_db, (end_db/4096), erro_global, flag_s_gilsondb_ativo);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}


int32_t gilsondb_create_add(const uint8_t key, const uint8_t tipo1, const uint8_t tipo2, const uint16_t cont_list_a, const uint16_t cont_list_b, const uint16_t cont_list_step)
{
	int32_t erro=erGILSONDB_OK;
	uint16_t off=0, nbytes=1;
	//uint8_t tipo_mux=0;

	if(flag_s_gilsondb_ativo==1)
	{
		// fazer a validacao de todos os parametros de entrada!!!!!!!!!!!!!!!
		// ...

		// 0baaabbbbb = a:tipo1, b=tipo2
		//tipo_mux = tipo1<<5;
		//tipo_mux |= tipo2;
		//printf("encode: 0baaabbbbb = a:tipo1(%u), b=tipo2()%u tipo_mux:%u\n", tipo1, tipo2, tipo_mux);

		if(s_gilsondb.size_header + 8 > SECTOR_SIZE_MEM)
		{
			// +8 é o pior caso...
			// ver se nao vai explodir o 'buf_db[]'
			erro = erGILSONDB_17;
			goto deu_erro;
		}

		// vamos calcular em 'off' o tamanho do offset header dessa chave
		off = s_gilsondb.size_header;  // vai começar em 'HEADER_DB'
		memcpy(&buf_db[off], &tipo1, 1);
		off += 1;
		memcpy(&buf_db[off], &tipo2, 1);
		off += 1;
		memcpy(&buf_db[off], &cont_list_a, 2);
		off += 2;
		memcpy(&buf_db[off], &cont_list_b, 2);
		off += 2;
		memcpy(&buf_db[off], &cont_list_step, 2);
		off += 2;

		s_gilsondb.size_header += (off-s_gilsondb.size_header);


		switch(tipo2)
		{
		case GSON_tINT8:
		case GSON_tUINT8:
			nbytes = 1;
			break;
		case GSON_tINT16:
		case GSON_tUINT16:
			nbytes = 2;
			break;
		case GSON_tINT32:
		case GSON_tUINT32:
			nbytes = 4;
			break;
		case GSON_tINT64:
		case GSON_tUINT64:
			nbytes = 8;
			break;
		case GSON_tFLOAT32:
			nbytes = 4;
			break;
		case GSON_tFLOAT64:
			nbytes = 8;
			break;
		case GSON_tSTRING:
			nbytes = 1;
			break;
		}

		// vamos calcular em 'off' que será o pior caso para cada pacote
		if(tipo1 == GSON_LIST)
		{
			if(tipo2==GSON_tSTRING)
			{
				off = nbytes * cont_list_a * cont_list_b;
				off += cont_list_a;  // para cada string tem 1 bytes do tamanho...
			}
			else
			{
				off = nbytes * cont_list_a;
			}
		}
		else if(tipo1 == GSON_MTX2D)
		{
			//off = nbytes * cont_list_a * cont_list_b;
			off = nbytes * cont_list_a * cont_list_step;
		}
		else  // GSON_SINGLE
		{
			if(tipo2==GSON_tSTRING)
			{
				off = nbytes * cont_list_a;
				off += 1;  // para cada string tem 1 bytes do tamanho...
			}
			else
			{
				off = nbytes;
			}
		}

		s_gilsondb.size_max_pack += off;

		/*
		if(s_gilsondb.max_keys == 255)
		{
			// limite max atingido
		}
		*/
		// parametro de entrada 'key' ainda nao é utilizado, mas da pra fazer validacao se a seguencia segue ordenada e crescente...
		s_gilsondb.max_keys += 1;
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}


	deu_erro:

#if (USO_DEBUG_LIB==1)
#if (TIPO_DEVICE==0)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf_DEBUG("DEBUG gilsondb_create_add::: erro:%i, key:%lu/%lu, size_header:%lu, size_max_pack:%lu(%u)\n", erro, key, s_gilsondb.max_keys, s_gilsondb.size_header, s_gilsondb.size_max_pack, OFF_PACK_GILSON_DB);
	}
#else  // PC
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG gilsondb_create_add::: erro:%i, key:%u/%u, size_header:%u, size_max_pack:%u(%u)\n", erro, key, s_gilsondb.max_keys, s_gilsondb.size_header, s_gilsondb.size_max_pack, OFF_PACK_GILSON_DB);
	}
#endif  // #if (TIPO_DEVICE==1)
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}



int32_t gilsondb_create_add_map(const uint16_t *map)
{
	/*
	'map' deve ter 6 'uint16_t'
	chave = map[0] (obrigatório)
	tipo1 = map[1] (obrigatório)
	tipo2 = map[2] (obrigatório)
	cont_list_a = map[3] (obrigatório mesmo que não utilizado)
	cont_list_b = map[4] (obrigatório mesmo que não utilizado)
	cont_list_step = map[5] (obrigatório mesmo que não utilizado)
	*/

	return gilsondb_create_add(map[0], map[1], map[2], map[3], map[4], map[5]);
}


int32_t gilsondb_create_end(const uint32_t end_db)
{
	uint32_t crc_header=0;
	int32_t erro=erGILSONDB_OK;

	if(flag_s_gilsondb_ativo==1)
	{
		if(_bitRead(s_gilsondb.configs_bits, egFixedSize)==0)
		{
			s_gilsondb.size_max_tot = s_gilsondb.max_packs * s_gilsondb.size_max_pack + s_gilsondb.size_header;
		}

		erro = _encode_header_db(&s_gilsondb, &buf_db[4], (HEADER_DB-4));

		if(erro==0)
		{
			//memcpy(&buf_db[4], &s_gilsondb, sizeof(s_gilsondb));
			// restante de 'buf_db' ja foi alocado nas camandas de 'gilsondb_create_add()'

			// grava tudo que alocou... e quantos bytes ocupa o HEADER total pois ele é dinamico e temos tambem um crc do header
			crc_header = gilsondb_crc(0xffffffff, &buf_db[4], (s_gilsondb.size_header - 4));

			memcpy(buf_db, &crc_header, 4);

			erro = mem_write_buff(end_db, buf_db, s_gilsondb.size_header);


		}

		flag_s_gilsondb_ativo = 0;
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}

#if (USO_DEBUG_LIB==1)
	//_print_header_db(&s_gilsondb, end_db);
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_create_end::: erro:%i, end_db:%lu(%lu)\n", erro, end_db, (end_db/4096));
#else  // PC
		printf("DEBUG gilsondb_create_end::: erro:%i, end_db:%u(%u)\n", erro, end_db, (end_db/4096));
#endif  // #if (TIPO_DEVICE==1)
	}

	/*
	// prova real...
	memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));
	// chama esse pois ja fechou o 'flag_s_gilsondb_ativo'!!!!
	_gilsondb_check_db_init(end_db, &s_gilsondb);
	_print_header_db(&s_gilsondb, end_db);
	*/

#endif  // #if (USO_DEBUG_LIB==1)

	memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));


	update_erro_global(erro);
	return erro;
}



int32_t gilsondb_add(const uint32_t end_db, uint8_t *data)
{
	uint32_t endereco=0, cont_ids=0, id_libre=0, status_id=0, id_cont=0, check_ids=0, len_pacote=0, crc1=0, crc2=0, size_bytes=0;
	int32_t erro=erGILSONDB_OK;
	uint8_t b[OFF_PACK_GILSON_DB];
	header_db s_gdb;

	erro = _gilsondb_statistics(end_db, &s_gdb, &cont_ids, &id_libre, &id_cont, &size_bytes);

	// para toda a 'data[]' sabemos que tem um offset de 16 bytes!!!
	// status_id + check_ids + tamanho do pacote + crc
	// e que está 100% compativel com o GILSON formatado no banco... isso nao é validado...
	// OBS: no pior caso temos que 'tamanho do pacote' == .size_max_pack

	if(erro==erGILSONDB_OK)
	{
		// tamanho do pacote novo, que está alocado na 'data'!!!
		memcpy(&len_pacote, &data[8], 4);
#if (USO_DEBUG_LIB==1)
		// debug ----------------------------------------------------
		memcpy(&crc1, &data[12], 4);  // debug
		crc2 = gilsondb_crc(0xffffffff, &data[OFF_PACK_GILSON_DB], len_pacote);
		//-----------------------------------------------------------
#endif  // #if (USO_DEBUG_LIB==1)


		if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
		{
			// em 'id_libre' teremos o endereco do proximo id
			endereco = id_libre;
			//id_libre = cont_ids;  para manter a logica correta do 'id_libre'
			endereco -= end_db;  // remove pois ja vem adicionado porem depois vamos adiciona-lo novamente!

			//printf("tcheeeeeeeeeeeeeeee endereco:%u e size_max_tot:%u\n", endereco, s_gdb.size_max_tot);
			if((endereco+len_pacote+OFF_PACK_GILSON_DB) > s_gdb.size_max_tot)
			{
				erro = erGILSONDB_31;
				goto deu_erro;
			}
		}
		else
		{
			endereco = id_libre * s_gdb.size_max_pack + s_gdb.size_header;

			//printf("tcheeeeeeeeeeeeeeee endereco:%u e size_max_tot:%u\n", endereco, s_gdb.size_max_tot);
			if(endereco > s_gdb.size_max_tot)  // if(endereco > ((s_gdb.max_packs * s_gdb.size_max_pack) + s_gdb.size_header))
			{
				// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
				// medir o alcance em bytes que vamos querer gravar ou ler...
				erro = erGILSONDB_3;
				goto deu_erro;
			}
		}

		// se desloca até offset do endereço do banco
		endereco += end_db;

		// análise do configs... do pacote antigo caso exista...
		// pra o 'egFixedSize' ativo, isso vai dar ruim pois vai estar errado
		mem_read_buff(endereco, OFF_PACK_GILSON_DB, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);

		/*
		// tamanho do pacote novo, que está alocado na 'data'!!!
		memcpy(&len_pacote, &data[8], 4);
#if (USO_DEBUG_LIB==1)
		// debug ----------------------------------------------------
		memcpy(&crc1, &data[12], 4);  // debug
		crc2 = gilsondb_crc(0xffffffff, &data[OFF_PACK_GILSON_DB], len_pacote);
		//-----------------------------------------------------------
#endif  // #if (USO_DEBUG_LIB==1)
		 */

		if(len_pacote==0 || len_pacote>(s_gdb.size_max_pack-OFF_PACK_GILSON_DB))
		{
			erro=erGILSONDB_20;
			goto deu_erro;
		}
		len_pacote += OFF_PACK_GILSON_DB;

		if(_bitRead(s_gdb.configs_bits, egCheckAddID)==1 && check_ids == s_gdb.check_ids)
		{
			// deixar ativo (ou mesmo se estiver ja estiver ativo)
			status_id |= 0x01;
		}
		else
		{
			// segue fluxo como sempre foi... independe de nada... vai incrementar 'id_cont'
			id_cont += 1;  // auto incrementa..., o sempre começa em 1 entao!
			status_id = id_cont&0xffffff;
			status_id <<= 8;
			status_id |= 0x01;  // indica id ativo
		}

		// status_id
		memcpy(data, &status_id, 4);

		// check_ids
		memcpy(&data[4], &s_gdb.check_ids, 4);

		// tamanho do pacote + crc ja estao alocados no buffer, assumimos que estão OK!!!

		erro = mem_write_buff(endereco, data, len_pacote);

		// valida gravacao???
		// tem que ir por partes... pois pode ser que nao temos um buffer do tamanho da data

	}


	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_add::: erro:%i, end_db:%lu(%lu), id:%lu, status_id:%lu, id_cont:%lu, len_pacote:%lu, crc:%lu|%lu, endereco:%lu\n", erro, end_db, (end_db/4096), id_libre, status_id, id_cont, len_pacote, crc1, crc2, endereco);
#else  // PC
		printf("DEBUG gilsondb_add::: erro:%i, end_db:%u(%u), id:%u, status_id:%u, id_cont:%u, len_pacote:%u, crc:%u|%u, endereco:%u\n", erro, end_db, (end_db/4096), id_libre, status_id, id_cont, len_pacote, crc1, crc2, endereco);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}



int32_t gilsondb_update(const uint32_t end_db, const uint32_t id, uint8_t *data)
{
	uint32_t endereco=0, status_id=0, check_ids=0, cont_ids=0, id_libre=0, id_cont=0, len_pacote=0, size_bytes=0;
	int32_t erro=erGILSONDB_OK;
	uint8_t b[OFF_PACK_GILSON_DB];
	header_db s_gdb;


	erro = _gilsondb_statistics(end_db, &s_gdb, &cont_ids, &id_libre, &id_cont, &size_bytes);

	if(erro==erGILSONDB_OK || erro==erGILSONDB_LOT)
	{
		if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
		{
			erro = erGILSONDB_22;
			goto deu_erro;
		}

		if(id>=s_gdb.max_packs)
		{
			erro = erGILSONDB_4;
			goto deu_erro;
		}

		endereco = id * s_gdb.size_max_pack + s_gdb.size_header;

		if(endereco > s_gdb.size_max_tot)  // if(endereco > ((s_gdb.max_packs * s_gdb.size_max_pack) + s_gdb.size_header))
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro = erGILSONDB_5;
			goto deu_erro;
		}

		// se desloca até offset do endereço do banco
		endereco += end_db;

		// análise do configs... do pacote antigo caso exista...
		mem_read_buff(endereco, OFF_PACK_GILSON_DB, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);

		// tamanho do pacote novo
		memcpy(&len_pacote, &data[8], 4);

		if(len_pacote==0 || len_pacote>(s_gdb.size_max_pack-OFF_PACK_GILSON_DB))
		{
			erro=erGILSONDB_21;
			goto deu_erro;
		}
		len_pacote += OFF_PACK_GILSON_DB;

		if(_bitRead(s_gdb.configs_bits, egCheckUpdID)==1 || check_ids == s_gdb.check_ids)
		{
			if(_bitRead(s_gdb.configs_bits, egCheckUpdID)==1)
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
			memcpy(data, &status_id, 4);

			// check_ids (vai manter o mesmo caso seja ok)
			memcpy(&data[4], &s_gdb.check_ids, 4);

			// tamanho do pacote + crc ja estao alocados no buffer, assumimos que estão OK!!!

			erro = mem_write_buff(endereco, data, len_pacote);

			// valida gravacao???
			// tem que ir por partes... pois pode ser que nao temos um buffer do tamanho da data

		}
		else
		{
			// quer atualizar um pacote que nao está ok com o atual banco...
			erro = erGILSONDB_6;
		}
	}


	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_update::: erro:%i, end_db:%lu(%lu), id:%lu, status_id:%lu, id_cont:%lu, endereco:%lu\n", erro, end_db, (end_db/4096), id, status_id, id_cont, endereco);
#else  // PC
		printf("DEBUG gilsondb_update::: erro:%i, end_db:%u(%u), id:%u, status_id:%u, id_cont:%u, endereco:%u\n", erro, end_db, (end_db/4096), id, status_id, id_cont, endereco);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}




int32_t gilsondb_del(const uint32_t end_db, const uint32_t id)
{
	uint32_t endereco=0, status_id, check_ids=0;
	int32_t erro=erGILSONDB_OK;
	uint8_t b[8];
	header_db s_gdb;

	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	if(erro==erGILSONDB_OK)
	{
		if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
		{
			erro = erGILSONDB_23;
			goto deu_erro;
		}

		if(id>=s_gdb.max_packs)
		{
			erro = erGILSONDB_7;
			goto deu_erro;
		}

		endereco = id * s_gdb.size_max_pack + s_gdb.size_header;

		if(endereco > ((s_gdb.max_packs * s_gdb.size_max_pack) + s_gdb.size_header))  // if(endereco > (s_gdb.max_packs * s_gdb.size_max_pack))
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro = erGILSONDB_8;
			goto deu_erro;
		}


		// se desloca até offset do endereço do banco
		endereco += end_db;

		// validar o pacote com check_ids e/ou crc??????
		mem_read_buff(endereco, 8, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);

		if(check_ids == s_gdb.check_ids)
		{
			// faz parte do atual banco...

			if((uint8_t)status_id == 0)
			{
				erro = erGILSONDB_DEL;  // quer deletar e ja está deletado (inativar)
				goto deu_erro;
			}

			// deixar inativo
			status_id &= 0xffffff00;

			// status_id
			memcpy(b, &status_id, 4);

			erro = mem_write_buff(endereco, b, 4);
		}
		// caso 'check_ids != s_neide.check_ids' só ignora e segue o baile, nada de erros (temos lixos antigos)
	}


	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_del::: erro:%i, end_db:%lu(%lu), id:%lu, status_id:%lu, endereco:%lu\n", erro, end_db, (end_db/4096), id, status_id, endereco);
#else  // PC
		printf("DEBUG gilsondb_del::: erro:%i, end_db:%u(%u), id:%u, status_id:%u, endereco:%u\n", erro, end_db, (end_db/4096), id, status_id, endereco);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}


int32_t gilsondb_read(const uint32_t end_db, const uint32_t id, uint8_t *data)
{
	uint32_t endereco, crc1=0, crc2=0, check_ids=0, id_cont=0, status_id=0, len_pacote=0, i;
	int32_t erro=erGILSONDB_OK;
	uint8_t valid, rOFF_PACK_GILSON_DB=0, i_banco=0;;
	uint8_t b[OFF_PACK_GILSON_DB+1]={0};
	header_db s_gdb;


	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	if(erro==erGILSONDB_OK)
	{
		if(id>=s_gdb.max_packs)
		{
			erro = erGILSONDB_9;
			goto deu_erro;
		}

		if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
		{
			if(s_gdb.multi_map>1)
			{
				rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB + 1;
			}
			else
			{
				rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB;
			}
			// vamos fazer uma busca linear no banco até chegar no id alvo
			endereco = s_gdb.size_header;  // 0 * s_gdb->size_max_pack + s_gdb->size_header
			endereco += end_db;
			for(i=0; i<s_gdb.max_packs; i++)
			{
				// análise do configs...
				erro = mem_read_buff(endereco, rOFF_PACK_GILSON_DB, b);
				memcpy(&status_id, b, 4);
				memcpy(&check_ids, &b[4], 4);
				memcpy(&len_pacote, &b[8], 4);
				memcpy(&crc2, &b[12], 4);
				if(s_gdb.multi_map>1)
				{
					i_banco = b[16];
				}

				if(check_ids == s_gdb.check_ids)
				{
					endereco += rOFF_PACK_GILSON_DB;

					if(len_pacote==0 || len_pacote>(s_gdb.size_max_pack-rOFF_PACK_GILSON_DB))
					{
						erro=erGILSONDB_25;
						goto deu_erro;
					}

					valid = (uint8_t)status_id&0xff;
					id_cont = (status_id>>8)&0xffffff;

					if(valid==1)
					{
						if(id == i)
						{
							// assume que o buffer 'data' vai conseguir alocar os dados resgatados!!!!
							erro = mem_read_buff(endereco, len_pacote, data);

							crc1 = gilsondb_crc(0xffffffff, data, len_pacote);

							if(crc1 == crc2)
							{
								// tudo certo e achamos nosso id a alocamos a data em 'data'
								break;
							}
							else
							{
								erro=erGILSONDB_27;
								goto deu_erro;
							}
						}

						//len_pacote += OFF_PACK_GILSON_DB;  nao precisa pois ja fizemos em 'endereco += OFF_PACK_GILSON_DB;'
						endereco += len_pacote;
						// resulta no endereço do proximo id
					}
					else
					{
						erro=erGILSONDB_26;
						goto deu_erro;
					}
				}
				else
				{
					erro=erGILSONDB_24;
					goto deu_erro;
				}
			}
		}
		else
		{
			endereco = id * s_gdb.size_max_pack + s_gdb.size_header;

			if(endereco > s_gdb.size_max_tot)  // if(endereco > ((s_gdb.max_packs * s_gdb.size_max_pack) + s_gdb.size_header))
			{
				// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
				// medir o alcance em bytes que vamos querer gravar ou ler...
				erro = erGILSONDB_10;
				goto deu_erro;
			}

			// se desloca até offset do endereço do banco
			endereco += end_db;

			// análise do configs...
			erro = mem_read_buff(endereco, OFF_PACK_GILSON_DB, b);
			memcpy(&status_id, b, 4);
			memcpy(&check_ids, &b[4], 4);
			memcpy(&len_pacote, &b[8], 4);
			memcpy(&crc2, &b[12], 4);

			if(check_ids == s_gdb.check_ids)
			{
				endereco += OFF_PACK_GILSON_DB;

				if(len_pacote==0 || len_pacote>(s_gdb.size_max_pack-OFF_PACK_GILSON_DB))
				{
					erro=erGILSONDB_19;
					goto deu_erro;
				}

				// assume que o buffer 'data' vai conseguir alocar os dados resgatados!!!!
				erro = mem_read_buff(endereco, len_pacote, data);

				crc1 = gilsondb_crc(0xffffffff, data, len_pacote);

				if(crc1 == crc2)
				{
					valid = (uint8_t)status_id&0xff;
					id_cont = (status_id>>8)&0xffffff;
					// validar o 'valid'?????
				}
				else
				{
					erro=erGILSONDB_11;
				}
			}
			else
			{
				erro=erGILSONDB_12;
			}
		}
	}


	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_read::: erro:%i, end_db:%lu(%lu), id:%lu, valid:%lu, id_cont:%lu, len_pacote:%lu, crc:%lu|%lu, endereco:%lu\n", erro, end_db, (end_db/4096), id, valid, id_cont, len_pacote, crc1, crc2, endereco);
#else  // PC
		printf("DEBUG gilsondb_read::: erro:%i, end_db:%u(%u), id:%u, valid:%u, id_cont:%u, len_pacote:%u, crc:%u|%u, endereco:%u\n", erro, end_db, (end_db/4096), id, valid, id_cont, len_pacote, crc1, crc2, endereco);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}



int32_t gilsondb_check(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint32_t codedb)
{
	uint32_t max_size=0, max2=0;
	int32_t erro=erGILSONDB_OK;
	header_db s_gdb;

	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	if(erro==erGILSONDB_OK)
	{
		if(s_gdb.code_db != codedb)
		{
			erro = erGILSONDB_2;
		}
		else if(max_packs!=0 && s_gdb.max_packs != max_packs)
		{
			erro = erGILSONDB_13;
		}
		//else if(offset_pack!=0 && (s_gdb.size_max_pack+OFF_PACK_GILSON_DB) != (offset_pack + OFF_PACK_GILSON_DB))
		else if(offset_pack!=0 && (offset_pack+OFF_PACK_GILSON_DB) > (s_gdb.size_max_pack))
		{
			erro = erGILSONDB_14;
		}
		else if(max_packs!=0 && offset_pack!=0)
		{
			max_size = (OFF_PACK_GILSON_DB + offset_pack) * max_packs;
			max2 = s_gdb.max_packs * s_gdb.size_max_pack;
			if(max_size > max2)  // > ou !=
			{
				erro = erGILSONDB_15;
			}
		}
	}

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		_print_header_db(&s_gdb, end_db);
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_check::: erro:%i, end_db:%lu(%lu), max_size:%lu|%lu\n", erro, end_db, (end_db/4096), max_size, max2);
#else  // PC
		printf("DEBUG gilsondb_check::: erro:%i, end_db:%u(%u), max_size:%u|%u\n", erro, end_db, (end_db/4096), max_size, max2);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}



int32_t gilsondb_get_valids(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids)
{
	uint32_t endereco, i, cont=0, status_id=0, check_ids=0, len_pacote=0, crc2=0;
	int32_t erro=erGILSONDB_OK;
	uint8_t valid=0, rOFF_PACK_GILSON_DB=0, i_banco=0;
	uint8_t b[OFF_PACK_GILSON_DB+1];
	header_db s_gdb;

	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	// OBS: nao faz validacao crc de cada pacote!!!

	if(erro==erGILSONDB_OK)
	{

		if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
		{
			if(s_gdb.multi_map>1)
			{
				rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB + 1;
			}
			else
			{
				rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB;
			}

			// vamos fazer uma busca linear no banco até chegar no id alvo
			endereco = s_gdb.size_header;  // 0 * s_gdb->size_max_pack + s_gdb->size_header
			endereco += end_db;
			for(i=0; i<s_gdb.max_packs; i++)
			{
				// análise do configs...
				erro = mem_read_buff(endereco, rOFF_PACK_GILSON_DB, b);
				memcpy(&status_id, b, 4);
				memcpy(&check_ids, &b[4], 4);
				memcpy(&len_pacote, &b[8], 4);
				memcpy(&crc2, &b[12], 4);
				if(s_gdb.multi_map>1)
				{
					i_banco = b[16];
				}

				if(s_gdb.check_ids == check_ids)
				{
					//endereco += OFF_PACK_GILSON_DB;

					if(len_pacote==0 || len_pacote>(s_gdb.size_max_pack-rOFF_PACK_GILSON_DB))
					{
						erro=erGILSONDB_28;
						break;
					}

					len_pacote += rOFF_PACK_GILSON_DB;

					valid = (uint8_t)status_id&0xff;

					if(valid==1)  // 'valid==1' cuidar pois pode haver 0 ou 255 indica que está vazio...
					{
						valids[cont]=i;
						cont+=1;

						endereco += len_pacote;
						// resulta no endereço do proximo id
					}
					else
					{
						// para pois ja temos problema logo vamos indicar que é um id libre e ai paramos por aqui
						break;
					}
				}
				else
				{
					// para pois ja temos problema logo vamos indicar que é um id libre e ai paramos por aqui
					break;
				}
			}
		}
		else
		{
			for(i=0; i<s_gdb.max_packs; i++)
			{
				endereco = i * s_gdb.size_max_pack + s_gdb.size_header;
				endereco += end_db;

				mem_read_buff(endereco, 8, b);
				memcpy(&status_id, b, 4);
				memcpy(&check_ids, &b[4], 4);

				if(s_gdb.check_ids == check_ids)
				{
					// validar os dados via crc????

					valid = (uint8_t)status_id&0xff;

					//printf("valid:%u\n", valid);
					if(valid==1)  // 'valid==1' cuidar pois pode haver 0 ou 255 indica que está vazio...
					{
						valids[cont]=i;
						cont+=1;
					}
				}
			}
		}
	}

	*cont_ids = cont;

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_get_valids::: erro:%i, end_db:%lu(%lu), cont:%lu\n", erro, end_db, (end_db/4096), cont);
#else  // PC
		printf("DEBUG gilsondb_get_valids::: erro:%i, end_db:%u(%u), cont:%u\n", erro, end_db, (end_db/4096), cont);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}


int32_t gilsondb_get_configs(const uint32_t end_db, const uint8_t tipo, uint32_t *config)
{
	uint32_t temp32, cont_ids=0, id_libre=0, id_cont=0, max1, max2=0, size_bytes=0;
	int32_t erro;
	header_db s_gdb;

	erro = _gilsondb_statistics(end_db, &s_gdb, &cont_ids, &id_libre, &id_cont, &size_bytes);

	if(erro==erGILSONDB_OK)
	{
		if(tipo==egCONT_IDS_DB)
		{
			*config = cont_ids;
		}
		else if(tipo==egID_LIBRE_DB)
		{
			*config = id_libre;
		}
		else if(tipo==egMAX_IDS_DB)
		{
			*config = s_gdb.max_packs;
		}
		else if(tipo==egOFF_IDS_DB)
		{
			max2 = (s_gdb.size_max_pack-OFF_PACK_GILSON_DB);  // remove parte "implicita" de 'OFF_INIT_DATA_DB'!!!
			*config = max2;
		}
		else if(tipo==egCODE_DB)
		{
			*config = s_gdb.code_db;
		}
		else if(tipo==egMAX_SIZE_DB)
		{
			//max2 = s_gdb.max_packs * s_gdb.size_max_pack + s_gdb.size_header;
			//*config = max2;

			*config = s_gdb.size_max_tot;  // independente do modo config_bits
		}
		else if(tipo==egCHECK_IDS_DB)
		{
			*config = s_gdb.check_ids;
		}
		else if(tipo==egID_CONT_DB)
		{
			*config = id_cont;
		}
		else if(tipo==egUSED_BYTES_DB)
		{
			if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
			{
				max1 = size_bytes;
			}
			else
			{
				max1 = cont_ids * s_gdb.size_max_pack + s_gdb.size_header;
			}
			*config = max1;
		}
		else if(tipo==egFREE_BYTES_DB)
		{
			//max2 = s_gdb.max_packs * s_gdb.size_max_pack + s_gdb.size_header;
			max2 = s_gdb.size_max_tot;

			if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
			{
				max1 = size_bytes;
			}
			else
			{
				max1 = cont_ids * s_gdb.size_max_pack + s_gdb.size_header;
			}
			temp32 = max2 - max1;
			*config = temp32;
		}
		else
		{
			erro = erGILSONDB_16;
			*config = 0;
		}
	}

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG neidedb_get_configs::: tipo:%u, erro:%i, end_db:%lu(%lu)\n", tipo, erro, end_db, (end_db/4096));
#else  // PC
		printf("DEBUG neidedb_get_configs::: tipo:%u, erro:%i, end_db:%u(%u)\n", tipo, erro, end_db, (end_db/4096));
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}


int32_t gilsondb_get_info(const uint32_t end_db, char *sms, const char *nome)
{
	uint32_t cont_ids=0, id_libre=0, id_cont=0, max2, size_bytes=0;
	int erro, i=0;
	header_db s_gdb={0};

	erro = _gilsondb_statistics(end_db, &s_gdb, &cont_ids, &id_libre, &id_cont, &size_bytes);
	max2 = s_gdb.max_packs * s_gdb.size_max_pack;

#if (TIPO_DEVICE==0)
	i = sprintf(sms, "---------------------------------\nBANCO:%s, END:%lu(%lu), VERSAO:%08lx, erro:%i\n"
			"\tMAX_PACKS:%lu, OFFSET_PACK:%lu(%u), CODE:%lu, max_size:%lu, check_ids:%lu, configs:0x%08lx, multi_map:%u\n"
			"\tcont:%lu, libre:%lu, id_cont:%lu\n---------------------------------\n",
			nome, end_db, (end_db/4096), s_gdb.versao, erro, s_gdb.max_packs,
			s_gdb.size_max_pack, OFF_PACK_GILSON_DB, s_gdb.code_db, max2, s_gdb.check_ids,
			s_gdb.configs_bits, s_gdb.multi_map,
			cont_ids, id_libre, id_cont);
#else  // PC
	i = sprintf(sms, "---------------------------------\nBANCO:%s, END:%u(%u), VERSAO:%08x, erro:%i\n"
			"\tMAX_PACKS:%u, OFFSET_PACK:%u(%u), CODE:%u, max_size:%u, check_ids:%u, configs:0x%08x, multi_map:%u\n"
			"\tcont:%u, libre:%u, id_cont:%u\n---------------------------------\n",
			nome, end_db, (end_db/4096), s_gdb.versao, erro, s_gdb.max_packs,
			s_gdb.size_max_pack, OFF_PACK_GILSON_DB, s_gdb.code_db, max2, s_gdb.check_ids,
			s_gdb.configs_bits, s_gdb.multi_map,
			cont_ids, id_libre, id_cont);
#endif  // #if (TIPO_DEVICE==1)

	return i;
}



#if (USO_DEBUG_LIB==1 || TIPO_DEVICE==1)

int gilsondb_info_deep(const uint32_t end_db, const char *nome_banco)
{
	uint32_t endereco, i, cont_ids=0, id_libre=0, status_id=0, id_cont=0, check_ids=0, maxx, crc1=0, len_pacote=0, size_bytes=0;  // crc2=0
	int erro;
	uint8_t valid=255, erro_FixedSize=0;
	uint8_t b[OFF_PACK_GILSON_DB+1], rOFF_PACK_GILSON_DB=0, i_banco=0;
	header_db s_gdb={0};

	erro = _gilsondb_statistics(end_db, &s_gdb, &cont_ids, &id_libre, &id_cont, &size_bytes);

	maxx = (s_gdb.max_packs * s_gdb.size_max_pack) + s_gdb.size_header;

	printf("gilsondb_info_deep: BANCO:%s\n", nome_banco);
	printf("END:%u, SETOR:%u, VERSAO:%08x, OFFSET_HEAD:%u, MAX_PACKS:%u, OFFSET_PACK:%u, CODE:%u, max_size:%u, check_ids:%u, configs:0x%08x size_bytes:%u/%u bytes, multi_map:%u, erro:%i\n",
			end_db, (end_db/4096), s_gdb.versao, s_gdb.size_header, s_gdb.max_packs,
			s_gdb.size_max_pack, s_gdb.code_db, maxx, s_gdb.check_ids,
			s_gdb.configs_bits, size_bytes, s_gdb.size_max_tot, s_gdb.multi_map, erro);

	if(erro==erGILSONDB_OK || erro==erGILSONDB_LOT)
	{
		if(s_gdb.multi_map>1)
		{
			rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB + 1;
		}
		else
		{
			rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB;
		}

	    //printf("| %-4s | %-10s | %-12s | %-12s | %-5s | %-8s |\n", "i", "endereco", "status_id", "check_ids", "valid", "id_cont");
	    //printf("|------|------------|--------------|--------------|-------|----------|\n");
	    printf("| %-4s | %-10s | %-12s | %-12s | %-5s | %-8s | %-10s | %-10s | %-5s |\n", "i", "endereco", "status_id", "check_ids", "valid", "id_cont", "len_pacote", "crc", "i_ban");
	    printf("|------|------------|--------------|--------------|-------|----------|------------|------------|-------|\n");

	    if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
	    {
			// vamos fazer uma busca linear no banco até chegar no id alvo
			endereco = s_gdb.size_header;  // 0 * s_gdb->size_max_pack + s_gdb->size_header
			endereco += end_db;
			for(i=0; i<s_gdb.max_packs; i++)
			{
				mem_read_buff(endereco, rOFF_PACK_GILSON_DB, b);
				memcpy(&status_id, b, 4);
				memcpy(&check_ids, &b[4], 4);
				memcpy(&len_pacote, &b[8], 4);
				memcpy(&crc1, &b[12], 4);
				if(s_gdb.multi_map>1)
				{
					i_banco = b[16];
				}
				if(check_ids == s_gdb.check_ids)
				{
					valid = (uint8_t)status_id&0xff;
					id_cont = (status_id>>8)&0xffffff;

					if(len_pacote==0 || len_pacote>(s_gdb.size_max_pack-rOFF_PACK_GILSON_DB))
					{
						erro=erGILSONDB_29;
						break;
					}

					if(valid==1)
					{
						len_pacote += rOFF_PACK_GILSON_DB;
						endereco += len_pacote;
						// resulta no endereço do proximo id
					}
					else
					{
						valid = 0;
						id_cont = 0;
						erro_FixedSize += 1;
					}
				}
				else
				{
					valid = 0;
					id_cont = 0;
					erro_FixedSize += 1;
				}

				if(erro_FixedSize>5)
				{
					// deixar mostrar 5 linhas pois sabemos que todas as outras sao invalidas!!!
					break;
				}


				printf("| %-4u | %-10u | %-12u | %-12u | %-5u | %-8u | %-10u | 0x%08X | %-5u |\n", i, endereco, status_id, check_ids, valid, id_cont, (len_pacote-OFF_PACK_GILSON_DB), crc1, i_banco);
		        printf("|------|------------|--------------|--------------|-------|----------|------------|------------|-------|\n");
			}
	    }
	    else
	    {
			for(i=0; i<s_gdb.max_packs; i++)
			{
				endereco = i * s_gdb.size_max_pack + s_gdb.size_header;
				endereco += end_db;
				mem_read_buff(endereco, OFF_PACK_GILSON_DB, b);
				memcpy(&status_id, b, 4);
				memcpy(&check_ids, &b[4], 4);
				memcpy(&len_pacote, &b[8], 4);
				memcpy(&crc1, &b[12], 4);
				//crc2 = gilsondb_crc(0xffffffff, &data[OFF_PACK_GILSON_DB], len_pacote);

				if(s_gdb.check_ids == check_ids)
				{
					valid = (uint8_t)status_id&0xff;
					id_cont = (status_id>>8)&0xffffff;
				}
				else
				{
					// é de uma versao antiga ou lixo...
					valid = 0;
					id_cont = 0;
					//crc1 = 0;
					//len_pacote=0;
				}

				//printf("| %u | %u | %u | %u | %u | %u |\n", i, endereco, status_id, check_ids, valid, id_cont);
				//printf("| %-3u | %-8u | %-9u | %-12u | %-5u | %-7u |\n", i, endereco, status_id, check_ids, valid, id_cont);
				//printf("|-----|------------|--------------|--------------|-------|----------|\n");
				printf("| %-4u | %-10u | %-12u | %-12u | %-5u | %-8u | %-10u | 0x%08X |\n", i, endereco, status_id, check_ids, valid, id_cont, len_pacote, crc1);
		        printf("|------|------------|--------------|--------------|-------|----------|------------|------------|\n");
			}
	    }
	    printf("\n");
	}

	return erro;
}

#else

int gilsondb_info_deep(const uint32_t end_db, const char *nome_banco)
{
	return 0;
}

#endif  // #if (USO_DEBUG_LIB==1)



//=================================================================================================================
//=================================================================================================================
// ENCODES e DECODES baseados no gilson modo=GSON_MODO_ZIP

int32_t gilsondb_encode_init(uint8_t *pack, const uint16_t size_max_pack)
{
	return gilson_encode_init(GSON_MODO_ZIP, pack, size_max_pack);
}


int32_t gilsondb_encode_end(uint32_t *crc)
{
	return gilson_encode_end(crc);
}


int32_t gilsondb_encode_mapfix(const uint16_t *map, const uint8_t *valor)
{
	/*
	'map' deve ter 6 'uint16_t'
	chave = map[0] (obrigatório)
	tipo1 = map[1] (obrigatório)
	tipo2 = map[2] (obrigatório)
	cont_list_a = map[3] (se for o caso... ou 0)
	cont_list_b = map[4] (se for o caso... ou 0)
	cont_list_step = map[5] (se for o caso... ou 0)
	*/

	// 'valor' sempre vai ser o maximo padrão ja definido no mapa do banco

	return gilson_encode_data(map[0], map[1], map[2], valor, map[3], map[4], map[5]);
}


int32_t gilsondb_encode_mapdin(const uint16_t *map, ...)
{
	uint16_t cont_list_a=0, cont_list_b=0, cont_list_step=0;
	uint8_t *valor;
	va_list argptr;

	/*
	'map' deve ter 3 'uint16_t'
	chave = map[0] (obrigatório)
	tipo1 = map[1] (obrigatório)
	tipo2 = map[2] (obrigatório)
	*/

	va_start(argptr, map);

	valor = va_arg(argptr, uint8_t *);

	if(map[1] == GSON_SINGLE)
	{
		if(map[2] == GSON_tSTRING)
		{
			cont_list_a = (uint16_t)va_arg(argptr, int);
		}
	}
	else if(map[1] == GSON_LIST)
	{
		cont_list_a = (uint16_t)va_arg(argptr, int);
		if(map[2] == GSON_tSTRING)
		{
			cont_list_b = (uint16_t)va_arg(argptr, int);
		}
	}
	else if(map[1] == GSON_MTX2D)
	{
		cont_list_a = (uint16_t)va_arg(argptr, int);
		cont_list_b = (uint16_t)va_arg(argptr, int);
		cont_list_step = (uint16_t)va_arg(argptr, int);
	}
	else
	{
		// erro
	}

	//printf("valor:%u\n", &valor);

	//printf("cont_list_a:%u, cont_list_b:%u, cont_list_step:%u\n", cont_list_a, cont_list_b, cont_list_step);

	va_end(argptr);

	return gilson_encode_data(map[0], map[1], map[2], valor, cont_list_a, cont_list_b, cont_list_step);
}



int32_t gilsondb_decode_init(const uint8_t *pack)
{
	uint8_t modo=0;
	return gilson_decode_init(pack, &modo);
}


int32_t gilsondb_decode_end(uint32_t *crc)
{
	return gilson_decode_end(crc);
}


int32_t gilsondb_decode_mapfix(const uint16_t *map, uint8_t *valor)
{
	/*
	'map' deve ter 6 'uint16_t'
	chave = map[0] (obrigatório)
	tipo1 = map[1] (obrigatório)
	tipo2 = map[2] (obrigatório)
	cont_list_a = map[3] (se for o caso... ou 0)
	cont_list_b = map[4] (se for o caso... ou 0)
	cont_list_step = map[5] (se for o caso... ou 0)
	*/

	return gilson_decode_data(map[0], map[1], map[2], valor, map[3], map[4], map[5]);
}

int32_t gilsondb_decode_mapdin(const uint16_t *map, ...)
{
	uint16_t cont_list_a=0, cont_list_b=0, cont_list_step=0;
	uint8_t *valor;
	va_list argptr;

	/*
	'map' deve ter 3 'uint16_t'
	chave = map[0] (obrigatório)
	tipo1 = map[1] (obrigatório)
	tipo2 = map[2] (obrigatório)
	*/

	va_start(argptr, map);

	valor = va_arg(argptr, uint8_t *);

	if(map[1] == GSON_SINGLE)
	{
		if(map[2] == GSON_tSTRING)
		{
			cont_list_a = (uint16_t)va_arg(argptr, int);
		}
	}
	else if(map[1] == GSON_LIST)
	{
		cont_list_a = (uint16_t)va_arg(argptr, int);
		if(map[2] == GSON_tSTRING)
		{
			cont_list_b = (uint16_t)va_arg(argptr, int);
		}
	}
	else if(map[1] == GSON_MTX2D)
	{
		cont_list_a = (uint16_t)va_arg(argptr, int);
		cont_list_b = (uint16_t)va_arg(argptr, int);
		cont_list_step = (uint16_t)va_arg(argptr, int);
	}
	else
	{
		// erro
	}

	va_end(argptr);

	return gilson_decode_data(map[0], map[1], map[2], valor, cont_list_a, cont_list_b, cont_list_step);
}



//=======================================================================================================================================
//=======================================================================================================================================
// para multi bancos no mesmo endereço via 'map'

int32_t gilsondb_create_multi_init(const uint32_t end_db, const uint32_t max_packs, const uint32_t codedb, const uint32_t max_bytes, const uint8_t n_bancos, const uint8_t *settings)
{
	int32_t erro=erGILSONDB_OK;
	uint8_t i;

	if(erro_global!=erGILSONDB_OCUPADO && flag_s_gilsondb_ativo==1)
	{
		erro_global = erGILSONDB_OK;
		flag_s_gilsondb_ativo = 0;
	}

	if(flag_s_gilsondb_ativo==0)
	{
		flag_s_gilsondb_ativo = 1;

		memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));
		memset(buf_db, 0x00, sizeof(buf_db));

		s_gilsondb.versao = VERSAO_GILSONDB;
		s_gilsondb.max_packs = max_packs;
		s_gilsondb.code_db = codedb;
		s_gilsondb.multi_map = n_bancos;

		//um dos parameteos vai ser esquema de operar em saltos fixos de pior caso ou sempre dinamico
		//mas dai só permite ler e add toda vida até o fim do limite...
		//senao é possivel operar tipo banco que edita, exclui, add...
		for(i=0; i<egLENMAX; i++)
		{
			if(settings[i])
			{
				_bitSet(s_gilsondb.configs_bits, i);
			}
		}
		//s_gilsondb.configs_bits=0;
		//s_gilsondb.configs[0] = settings[0];
		//s_gilsondb.configs[1] = settings[1];
		//s_gilsondb.configs[2] = settings[2];
		//s_gilsondb.configs[3] = settings[3];

		if(_bitRead(s_gilsondb.configs_bits, egFixedSize)==1)
		{
			if(max_bytes==0)
			{
				erro = erGILSONDB_32;
				goto deu_erro;
			}
			s_gilsondb.size_max_tot = max_bytes;
		}
		else
		{
			// ate entao só funciona para modo 'egFixedSize' ativo!!!!
			erro = erGILSONDB_33;
			goto deu_erro;
		}
		// la em 'gilsondb_create_end()' vai atualizar com o valor final de 's_gilsondb.size_max_tot' para todos os modos!!!




		//-------------------------------------------------------------------------------------------------------
		// bruxaria para gerar o 'check_ids' usando uma mistura de crc+seed_prng
		// vamos ver se ja nao existia algo nesse endereço da memória... analisando o id==0
		if(init_seed_prng==0)
		{
			// semente inicial iniciadaaaaaaaaaaaaaaaaa nesse instante existente
			mem_read_buff(end_db, (666+HEADER_DB), buf_db);
			seed_prng = gilsondb_crc(0xffffffff, buf_db, (666+HEADER_DB));
			init_seed_prng=1;
			// vai servir de base para todos os próximo casos de uso quando formos criar um banco
		}
		s_gilsondb.check_ids = gilsondb_prng();
		//-------------------------------------------------------------------------------------------------------

		s_gilsondb.size_header = HEADER_DB;  // crc + header, começa assim e vai incrementando a cada chamada de 'gilsondb_create_add()'

		s_gilsondb.size_max_pack = OFF_PACK_GILSON_DB;  // pois todos pacote independente de quantos bytes terá (se dinamico) sempre vai ter 'OFF_PACK_GILSON_DB' no inicio
		s_gilsondb.size_max_pack += 1;  // é o offset de indentificação do pacote no modo 'GSON_MODO_ZIP' o chamado 'OFFSET_MODO_ZIP'
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}

	deu_erro:

#if (USO_DEBUG_LIB==1)
	_print_header_db(&s_gilsondb, end_db);
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_create_multi_init::: erro:%i, end_db:%lu(%lu), erro_global:%i, ativo:%u\n", erro, end_db, (end_db/4096), erro_global, flag_s_gilsondb_ativo);
#else  // PC
		printf("DEBUG gilsondb_create_multi_init::: erro:%i, end_db:%u(%u), erro_global:%i, ativo:%u\n", erro, end_db, (end_db/4096), erro_global, flag_s_gilsondb_ativo);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}


int32_t gilsondb_create_multi_add_map(const uint8_t i_banco, const uint8_t n_chaves, const uint16_t map[][6])
{
	int32_t erro=erGILSONDB_OK;
	uint16_t size_map=0, off=0, nbytes=1, i;
	//uint8_t tipo_mux=0;

	if(flag_s_gilsondb_ativo==1)
	{
		/*
		'map' deve ter 6 'uint16_t'
		chave = map[0] (obrigatório)
		tipo1 = map[1] (obrigatório)
		tipo2 = map[2] (obrigatório)
		cont_list_a = map[3] (se for o caso... ou 0)
		cont_list_b = map[4] (se for o caso... ou 0)
		cont_list_step = map[5] (se for o caso... ou 0)
		*/

		if(i_banco >= s_gilsondb.multi_map)
		{
			erro = erGILSONDB_34;
			goto deu_erro;
		}

		// fazer a validacao de todos os parametros de entrada!!!!!!!!!!!!!!!
		// ?????

		// OBS: 's_gilsondb.multi_map' vai ditar quantos 'map' vai conter serializados no 's_gilsondb.size_header'

		size_map = (uint16_t)n_chaves*6*2;  // ====>>>> map[n_chaves][6]
		if(s_gilsondb.size_header + (size_map+4) > SECTOR_SIZE_MEM)
		{
			// +8 é o pior caso...
			// ver se nao vai explodir o 'buf_db[]'
			erro = erGILSONDB_35;
			goto deu_erro;
		}

		// vamos calcular em 'off' o tamanho do offset header dessa chave
		off = s_gilsondb.size_header;  // vai começar em 'HEADER_DB'

		// para cada mapa a ser gravado os 2 primeiros bytes sao tamanho do mapa, seguido do mapa
		memcpy(&buf_db[off], &size_map, 2);
		off += 2;
		memcpy(&buf_db[off], (uint8_t *)&map, size_map);
		off += size_map;

		s_gilsondb.size_header += (off-s_gilsondb.size_header);

		for(i=0; i<n_chaves; i++)
		{
			switch(map[i][2])
			{
			case GSON_tINT8:
			case GSON_tUINT8:
				nbytes = 1;
				break;
			case GSON_tINT16:
			case GSON_tUINT16:
				nbytes = 2;
				break;
			case GSON_tINT32:
			case GSON_tUINT32:
				nbytes = 4;
				break;
			case GSON_tINT64:
			case GSON_tUINT64:
				nbytes = 8;
				break;
			case GSON_tFLOAT32:
				nbytes = 4;
				break;
			case GSON_tFLOAT64:
				nbytes = 8;
				break;
			case GSON_tSTRING:
				nbytes = 1;
				break;
			}

			// vamos calcular em 'off' que será o pior caso para cada pacote
			if(map[i][1] == GSON_LIST)
			{
				if(map[i][2]==GSON_tSTRING)
				{
					off = nbytes * map[i][3] * map[i][4];
					off += map[i][3];  // para cada string tem 1 bytes do tamanho...
				}
				else
				{
					off = nbytes * map[i][3];
				}
			}
			else if(map[i][1] == GSON_MTX2D)
			{
				//off = nbytes * cont_list_a * cont_list_b;
				off = nbytes * map[i][3] * map[i][4];
			}
			else  // GSON_SINGLE
			{
				if(map[i][2]==GSON_tSTRING)
				{
					off = nbytes * map[i][3];
					off += 1;  // para cada string tem 1 bytes do tamanho...
				}
				else
				{
					off = nbytes;
				}
			}

			s_gilsondb.size_max_pack += off;  // vai ter a soma de todos os mapas!!!!!
		}

		/*
		if(s_gilsondb.max_keys == 255)
		{
			// limite max atingido
		}
		*/
		// parametro de entrada 'key' ainda nao é utilizado, mas da pra fazer validacao se a seguencia segue ordenada e crescente...
		s_gilsondb.max_keys += n_chaves;
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}


	deu_erro:

#if (USO_DEBUG_LIB==1)
#if (TIPO_DEVICE==0)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf_DEBUG("DEBUG gilsondb_create_multi_add_map::: erro:%i, key:%lu/%lu, size_header:%lu, size_max_pack:%lu(%u)\n", erro, key, s_gilsondb.max_keys, s_gilsondb.size_header, s_gilsondb.size_max_pack, OFF_PACK_GILSON_DB);
	}
#else  // PC
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG gilsondb_create_multi_add_map::: erro:%i, key:%u/%u, size_header:%u, size_max_pack:%u(%u)\n", erro, key, s_gilsondb.max_keys, s_gilsondb.size_header, s_gilsondb.size_max_pack, OFF_PACK_GILSON_DB);
	}
#endif  // #if (TIPO_DEVICE==1)
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}



int32_t gilsondb_create_multi_end(const uint32_t end_db)
{
	uint32_t crc_header=0;
	int32_t erro=erGILSONDB_OK;

	if(flag_s_gilsondb_ativo==1)
	{
		/*
		nao tem isso ainda... somente modo 'egFixedSize'
		if(_bitRead(s_gilsondb.configs_bits, egFixedSize)==0)
		{
			s_gilsondb.size_max_tot = s_gilsondb.max_packs * s_gilsondb.size_max_pack + s_gilsondb.size_header;
		}
		*/

		erro = _encode_header_db(&s_gilsondb, &buf_db[4], (HEADER_DB-4));

		if(erro==0)
		{
			//memcpy(&buf_db[4], &s_gilsondb, sizeof(s_gilsondb));
			// restante de 'buf_db' ja foi alocado nas camandas de 'gilsondb_create_add()'

			// grava tudo que alocou... e quantos bytes ocupa o HEADER total pois ele é dinamico e temos tambem um crc do header
			crc_header = gilsondb_crc(0xffffffff, &buf_db[4], (s_gilsondb.size_header - 4));

			memcpy(buf_db, &crc_header, 4);

			erro = mem_write_buff(end_db, buf_db, s_gilsondb.size_header);
		}

		flag_s_gilsondb_ativo = 0;
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}

#if (USO_DEBUG_LIB==1)
	//_print_header_db(&s_gilsondb, end_db);
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_create_multi_end::: erro:%i, end_db:%lu(%lu)\n", erro, end_db, (end_db/4096));
#else  // PC
		printf("DEBUG gilsondb_create_multi_end::: erro:%i, end_db:%u(%u)\n", erro, end_db, (end_db/4096));
#endif  // #if (TIPO_DEVICE==1)
	}

	/*
	// prova real...
	memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));
	// chama esse pois ja fechou o 'flag_s_gilsondb_ativo'!!!!
	_gilsondb_check_db_init(end_db, &s_gilsondb);
	_print_header_db(&s_gilsondb, end_db);
	*/

#endif  // #if (USO_DEBUG_LIB==1)

	memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));


	update_erro_global(erro);
	return erro;
}



int32_t gilsondb_multi_add(const uint32_t end_db, const uint8_t i_banco, uint8_t *data)
{
	uint32_t endereco=0, cont_ids=0, id_libre=0, status_id=0, id_cont=0, check_ids=0, len_pacote=0, crc1=0, crc2=0, size_bytes=0;
	int32_t erro=erGILSONDB_OK;
	uint8_t b[OFF_PACK_GILSON_DB+1];
	header_db s_gdb;

	erro = _gilsondb_statistics(end_db, &s_gdb, &cont_ids, &id_libre, &id_cont, &size_bytes);

	// para toda a 'data[]' sabemos que tem um offset de 16 bytes!!!
	// status_id + check_ids + tamanho do pacote + crc
	// e que está 100% compativel com o GILSON formatado no banco... isso nao é validado...
	// OBS: no pior caso temos que 'tamanho do pacote' == .size_max_pack

	if(erro==erGILSONDB_OK)
	{
		// tamanho do pacote novo, que está alocado na 'data'!!!
		memcpy(&len_pacote, &data[8], 4);
#if (USO_DEBUG_LIB==1)
		// debug ----------------------------------------------------
		memcpy(&crc1, &data[12], 4);  // debug
		crc2 = gilsondb_crc(0xffffffff, &data[OFF_PACK_GILSON_DB+1], len_pacote);
		//-----------------------------------------------------------
#endif  // #if (USO_DEBUG_LIB==1)

		if(i_banco != data[16])
		{
			erro = erGILSONDB_36;
			goto deu_erro;
		}

		if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
		{
			// em 'id_libre' teremos o endereco do proximo id
			endereco = id_libre;
			//id_libre = cont_ids;  para manter a logica correta do 'id_libre'
			endereco -= end_db;  // remove pois ja vem adicionado porem depois vamos adiciona-lo novamente!

			//printf("tcheeeeeeeeeeeeeeee endereco:%u e size_max_tot:%u\n", endereco, s_gdb.size_max_tot);
			if((endereco+len_pacote+OFF_PACK_GILSON_DB+1) > s_gdb.size_max_tot)
			{
				erro = erGILSONDB_37;
				goto deu_erro;
			}
		}
		else
		{
			erro = erGILSONDB_38;
			goto deu_erro;
			/*
			endereco = id_libre * s_gdb.size_max_pack + s_gdb.size_header;

			//printf("tcheeeeeeeeeeeeeeee endereco:%u e size_max_tot:%u\n", endereco, s_gdb.size_max_tot);
			if(endereco > s_gdb.size_max_tot)  // if(endereco > ((s_gdb.max_packs * s_gdb.size_max_pack) + s_gdb.size_header))
			{
				// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
				// medir o alcance em bytes que vamos querer gravar ou ler...
				erro = erGILSONDB_3;
				goto deu_erro;
			}
			*/
		}

		// se desloca até offset do endereço do banco
		endereco += end_db;

		// análise do configs... do pacote antigo caso exista...
		// pra o 'egFixedSize' ativo, isso vai dar ruim pois vai estar errado
		mem_read_buff(endereco, OFF_PACK_GILSON_DB+1, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);

		/*
		// tamanho do pacote novo, que está alocado na 'data'!!!
		memcpy(&len_pacote, &data[8], 4);
#if (USO_DEBUG_LIB==1)
		// debug ----------------------------------------------------
		memcpy(&crc1, &data[12], 4);  // debug
		crc2 = gilsondb_crc(0xffffffff, &data[OFF_PACK_GILSON_DB], len_pacote);
		//-----------------------------------------------------------
#endif  // #if (USO_DEBUG_LIB==1)
		 */

		if(len_pacote==0 || len_pacote>(s_gdb.size_max_pack-OFF_PACK_GILSON_DB+1))
		{
			erro=erGILSONDB_39;
			goto deu_erro;
		}
		len_pacote += (OFF_PACK_GILSON_DB+1);

		if(_bitRead(s_gdb.configs_bits, egCheckAddID)==1 && check_ids == s_gdb.check_ids)
		{
			// deixar ativo (ou mesmo se estiver ja estiver ativo)
			status_id |= 0x01;
		}
		else
		{
			// segue fluxo como sempre foi... independe de nada... vai incrementar 'id_cont'
			id_cont += 1;  // auto incrementa..., o sempre começa em 1 entao!
			status_id = id_cont&0xffffff;
			status_id <<= 8;
			status_id |= 0x01;  // indica id ativo
		}

		// status_id
		memcpy(data, &status_id, 4);

		// check_ids
		memcpy(&data[4], &s_gdb.check_ids, 4);

		// tamanho do pacote + crc + i_banco ja estao alocados no buffer, assumimos que estão OK!!!

		erro = mem_write_buff(endereco, data, len_pacote);

		// valida gravacao???
		// tem que ir por partes... pois pode ser que nao temos um buffer do tamanho da data

	}


	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_multi_add::: erro:%i, end_db:%lu(%lu), id:%lu, status_id:%lu, id_cont:%lu, len_pacote:%lu, crc:%lu|%lu, endereco:%lu, i_banco:%u|%u\n", erro, end_db, (end_db/4096), id_libre, status_id, id_cont, len_pacote, crc1, crc2, endereco);
#else  // PC
		printf("DEBUG gilsondb_multi_add::: erro:%i, end_db:%u(%u), id:%u, status_id:%u, id_cont:%u, len_pacote:%u, crc:%u|%u, endereco:%u, i_banco:%u|%u\n", erro, end_db, (end_db/4096), id_libre, status_id, id_cont, len_pacote, crc1, crc2, endereco);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}


int32_t gilsondb_get_multi_valids(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids, uint8_t *i_bancos)
{
	uint32_t endereco, i, cont=0, status_id=0, check_ids=0, len_pacote=0, crc2=0;
	int32_t erro=erGILSONDB_OK;
	uint8_t valid=0, rOFF_PACK_GILSON_DB=0, i_banco=0;
	uint8_t b[OFF_PACK_GILSON_DB+1];
	header_db s_gdb;

	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	// OBS: nao faz validacao crc de cada pacote!!!

	if(erro==erGILSONDB_OK)
	{
		if(s_gdb.multi_map==1)
		{
			erro=erGILSONDB_40;
			goto deu_erro;
		}

		if(_bitRead(s_gdb.configs_bits, egFixedSize)==1)
		{
			if(s_gdb.multi_map>1)
			{
				rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB + 1;
			}
			else
			{
				rOFF_PACK_GILSON_DB = OFF_PACK_GILSON_DB;
			}

			// vamos fazer uma busca linear no banco até chegar no id alvo
			endereco = s_gdb.size_header;  // 0 * s_gdb->size_max_pack + s_gdb->size_header
			endereco += end_db;
			for(i=0; i<s_gdb.max_packs; i++)
			{
				// análise do configs...
				erro = mem_read_buff(endereco, rOFF_PACK_GILSON_DB, b);
				memcpy(&status_id, b, 4);
				memcpy(&check_ids, &b[4], 4);
				memcpy(&len_pacote, &b[8], 4);
				memcpy(&crc2, &b[12], 4);
				if(s_gdb.multi_map>1)
				{
					i_banco = b[16];
				}

				if(s_gdb.check_ids == check_ids)
				{
					//endereco += OFF_PACK_GILSON_DB;

					if(len_pacote==0 || len_pacote>(s_gdb.size_max_pack-rOFF_PACK_GILSON_DB))
					{
						erro=erGILSONDB_41;
						break;
					}

					len_pacote += rOFF_PACK_GILSON_DB;

					valid = (uint8_t)status_id&0xff;

					if(valid==1)  // 'valid==1' cuidar pois pode haver 0 ou 255 indica que está vazio...
					{
						valids[cont]=i;
						i_bancos[cont]=i_banco;

						cont+=1;

						endereco += len_pacote;
						// resulta no endereço do proximo id
					}
					else
					{
						// para pois ja temos problema logo vamos indicar que é um id libre e ai paramos por aqui
						break;
					}
				}
				else
				{
					// para pois ja temos problema logo vamos indicar que é um id libre e ai paramos por aqui
					break;
				}
			}
		}
		// ainda nao temos o modo fixo size

	}

	deu_erro:

	*cont_ids = cont;

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
#if (TIPO_DEVICE==0)
		printf_DEBUG("DEBUG gilsondb_get_valids::: erro:%i, end_db:%lu(%lu), cont:%lu\n", erro, end_db, (end_db/4096), cont);
#else  // PC
		printf("DEBUG gilsondb_get_valids::: erro:%i, end_db:%u(%u), cont:%u\n", erro, end_db, (end_db/4096), cont);
#endif  // #if (TIPO_DEVICE==1)
	}
#endif  // #if (USO_DEBUG_LIB==1)


	update_erro_global(erro);
	return erro;
}



//=======================================================================================================================================
//=======================================================================================================================================


