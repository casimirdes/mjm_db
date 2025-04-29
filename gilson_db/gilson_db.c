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


#include "../emu_flash_nor/flash_nor.h"  // "camada de baixo nível" da memória
#include "../gilson_c/gilson.h"

#include "gilson_db.h"

#define USO_DEBUG_LIB		1  // 0=microcontrolador, 1=PC
#define PRINT_DEBUG			1  // 1 = printa toda vida o debug


// code check gravado em todos esquemas que envolve ou nao banco...
#define CODE_DB_CHECK		1354785413UL	// fixo e igual para todos!!!

#define LIMIT_KEYS			256  	// esse valor é duplicado "tipo1"+"tipo2" uint8_t (limite de 256 chaves de cada tabela/banco)
#define HEADER_DB			36  	// 4+32 tamanho de offset de cabeçalho de configuracoes do banco...
#define SECTOR_SIZE_MEM		4096	// supondo uma memória flash NOR que é por setores fixos, aqui temos o tamanho em bytes do setor

#define VERSAO_GILSONDB		0x4d4a4d64  // "MJM"64


typedef struct
{
	// MANTER ESSA ORDEM!!!!! e até 32 'HEADER_DB' bytes!!!!
	uint32_t versao;			// 4 primeiros bytes sao a 'marca' = "MJM2"
	uint32_t max_packs;  		// maximo de pacotes armazenados
	uint32_t code_db;  			// código para validar criacao do banco na posicao da memoria que será gravada
	uint32_t check_ids;			// esquema de validador que muda toda vez que o banco é criado/limpo onde cada id grava esse valor no pacote
	uint32_t size_max_pack;		// tamanho do pacote 'data' (no pior caso) nao soma o 'OFF_PACK_GILSON_DB'

	uint8_t configs[4];			// [0]=auto_loop, [1]=check_update_id

	uint16_t size_header;     	// tamanho do pacote header, vai ser dinamico agora... (da pra ser um uint16_t)
	uint16_t reserva16;

	uint8_t max_keys;			// total de chaves do gilson, até 255 (0 a 255 = 256)
	uint8_t reserv2;
	uint8_t reserv3;
	uint8_t reserv4;

	// OBS: max 32 bytes!!!!!!!!!!!!
}header_db;


enum e_config_gilsondb
{
	eFixedSize,		// 0=ativado cada pack de id é gravado com offset do pior caso para poder termos add/update/delete como um banco normal, 1=somente add data dinamica e invativar
	eAutoLoop, 		// flag de configuração do auto_loop
	eCheckUpdID,	// flag validar ou nao 'check_ids' na funcao de update id
	eCheckAddID,	// flag add no lugar de inativo e válido nao muda id_cont
};


static uint8_t buf_db[SECTOR_SIZE_MEM];  // base no setor da memoria que vale 4096 bytes, 3 setores...
static uint32_t seed_prng = 667;  // pobre e sempre iguallll, pensar em algo melhor...
static uint8_t init_seed_prng = 0;

static header_db s_gilsondb;  // para uso somente quando estamos criando um banco
static uint8_t flag_s_gilsondb_ativo = 0;

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






// valida as 2 variaveis que temos certeza que devem estar com esses valores define
static int _gilsondb_check_db_init(const uint32_t end_db, header_db *s_gdb)
{
	uint32_t crc1=0, crc2=0;
	int erro;

	if(flag_s_gilsondb_ativo==0)
	{
		erro = mem_read_buff(end_db, HEADER_DB, buf_db);

		if(erro==erGILSONDB_OK)
		{
			memcpy(&crc1, buf_db, 4);
			memcpy(s_gdb, &buf_db[4], sizeof(*s_gdb));

			erro = mem_read_buff(end_db+4, (s_gdb->size_header - 4), buf_db);  // le o restante do header completo!

			crc2 = gilsondb_crc(0xffffffff, buf_db, (s_gdb->size_header - 4));

			if(crc1 != crc2)
			{
				erro = erGILSONDB_0;
			}
			else if(s_gdb->versao != VERSAO_GILSONDB)
			{
				erro = erGILSONDB_1;
			}
			else if(s_gdb->code_db != CODE_DB_CHECK)
			{
				erro = erGILSONDB_2;
			}
			// daria para validar o 's_neide->check_ids' geral para com o id=0??? pois tem que estar de acordo...
		}
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG _gilsondb_check_db_init::: erro:%i\n", erro);
	}
#endif

	return erro;
}


static int _gilsondb_statistics(const uint32_t end_db, header_db *s_gdb, uint32_t *cont_ids_, uint32_t *id_libre_, uint32_t *id_cont_)
{
	uint32_t endereco, i, cont_ids=0, id_libre=0, status_id=0, id_cont=0, id_cont_maior=0, check_ids=0, id_cont_menor=0xffffffff, index_maior=0, index_menor=0;
	int erro;
	uint8_t valid=255, set_libre=0;
	uint8_t b[OFF_PACK_GILSON_DB];

	erro = _gilsondb_check_db_init(end_db, s_gdb);

	if(erro==erGILSONDB_OK)
	{
		for(i=0; i<s_gdb->max_packs; i++)
		{
			// quando 'eFixedSize' está ativado???? nada ainda...
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
				if(s_gdb->configs[eAutoLoop])
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

	*cont_ids_ = cont_ids;
	*id_libre_ = id_libre;
	*id_cont_ = id_cont_maior;

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG _gilsondb_statistics::: erro:%i, end_db:%u, cont_ids:%u, id_libre:%u, id_cont_maior:%u(%u), id_cont_menor:%u(%u)\n", erro, end_db, cont_ids, id_libre, id_cont_maior, index_maior, id_cont_menor, index_menor);
	}
#endif

	return erro;
}

#if (USO_DEBUG_LIB==1)
static void _print_header_db(const header_db *s_gdb)
{
	printf("DEBUG _print_header_db::: versao:%08x, max_packs:%u, code_db:%u, check_ids:%u, size_max_pack:%u, configs:[%u, %u, %u, %u], size_header:%u, max_keys:%u\n",
			s_gdb->versao,
			s_gdb->max_packs,
			s_gdb->code_db,
			s_gdb->check_ids,
			s_gdb->size_max_pack,
			s_gdb->configs[0], s_gdb->configs[1], s_gdb->configs[2], s_gdb->configs[3],
			s_gdb->size_header,
			s_gdb->max_keys);

}
#endif  // #if (USO_DEBUG_LIB==1)


int gilsondb_init(void)
{
	return mem_init();
}


// https://www.youtube.com/watch?v=rtGACXTp6mY
int gilsondb_create_init(const uint32_t end_db, const uint32_t max_packs, const uint8_t *settings)
{
	int erro=erGILSONDB_OK;

	if(flag_s_gilsondb_ativo==0)
	{
		flag_s_gilsondb_ativo = 1;

		memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));
		memset(buf_db, 0x00, sizeof(buf_db));

		s_gilsondb.versao = VERSAO_GILSONDB;
		s_gilsondb.max_packs = max_packs;
		s_gilsondb.code_db = CODE_DB_CHECK;

		//um dos parameteos vai ser esquema de operar em saltos fixos de pior caso ou sempre dinamico
		//mas dai só permite ler e add toda vida até o fim do limite...
		//senao é possivel operar tipo banco que edita, exclui, add...

		s_gilsondb.configs[0] = settings[0];
		s_gilsondb.configs[1] = settings[1];
		s_gilsondb.configs[2] = settings[2];
		s_gilsondb.configs[3] = settings[3];

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

		s_gilsondb.size_max_pack = 1;  // é o offset de indentificação do pacote no modo 'GSON_MODO_ZIP' o chamado 'OFFSET_MODO_ZIP'

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

#if (USO_DEBUG_LIB==1)
	_print_header_db(&s_gilsondb);
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG gilsondb_create_init::: erro:%i, end_db:%u\n", erro, end_db);
	}
#endif

	return erro;
}


int gilsondb_create_add(const uint8_t key, const uint8_t tipo1, const uint8_t tipo2, const uint16_t cont_list_a, const uint16_t cont_list_b, const uint16_t cont_list_step)
{
	int erro=erGILSONDB_OK;
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

#if (USO_DEBUG_LIB==1)
	printf("DEBUG gilsondb_create_add::: key:%u/%u, size_header:%u, size_max_pack:%u\n", key, s_gilsondb.max_keys, s_gilsondb.size_header, s_gilsondb.size_max_pack);
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG gilsondb_create_add::: erro:%i\n", erro);
	}
#endif

	return erro;
}


int gilsondb_create_end(const uint32_t end_db)
{
	uint32_t crc_header=0;
	int erro=erGILSONDB_OK;

	if(flag_s_gilsondb_ativo==1)
	{
		memcpy(&buf_db[4], &s_gilsondb, sizeof(s_gilsondb));
		// restante de 'buf_db' ja foi alocado nas camandas de 'gilsondb_create_add()'

		// grava tudo que alocou... e quantos bytes ocupa o HEADER total pois ele é dinamico e temos tambem um crc do header
		crc_header = gilsondb_crc(0xffffffff, &buf_db[4], (s_gilsondb.size_header - 4));

		memcpy(buf_db, &crc_header, 4);

		erro = mem_write_buff(end_db, buf_db, s_gilsondb.size_header);

		flag_s_gilsondb_ativo = 0;
	}
	else
	{
		erro = erGILSONDB_OCUPADO;
	}

#if (USO_DEBUG_LIB==1)

	_print_header_db(&s_gilsondb);
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG gilsondb_create_end::: erro:%i, end_db:%u\n", erro, end_db);
	}

	// prova real...
	memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));
	_gilsondb_check_db_init(end_db, &s_gilsondb);
	_print_header_db(&s_gilsondb);

#endif

	memset(&s_gilsondb, 0x00, sizeof(s_gilsondb));

	return erro;
}



int gilsondb_add(const uint32_t end_db, uint8_t *data)
{
	uint32_t endereco=0, cont_ids=0, id_libre=0, status_id=0, id_cont=0, check_ids=0, len_pacote=0;
	int erro=erGILSONDB_OK;
	uint8_t b[OFF_PACK_GILSON_DB];
	header_db s_gdb;

	erro = _gilsondb_statistics(end_db, &s_gdb, &cont_ids, &id_libre, &id_cont);

	// para toda a 'data[]' sabemos que tem um offset de 16 bytes!!!
	// status_id + check_ids + tamanho do pacote + crc
	// e que está 100% compativel com o GILSON formatado no banco... isso nao é validado...
	// OBS: no pior caso temos que 'tamanho do pacote' == .size_max_pack

	if(erro==erGILSONDB_OK)
	{
		endereco = id_libre * s_gdb.size_max_pack + s_gdb.size_header;

		if(endereco > (s_gdb.max_packs * s_gdb.size_max_pack))
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro = erGILSONDB_3;
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
		len_pacote += OFF_PACK_GILSON_DB;

		if(s_gdb.configs[eCheckAddID]==1 && check_ids == s_gdb.check_ids)
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
		printf("DEBUG gilsondb_add::: erro:%i, end_db:%u, id:%u, status_id:%u, id_cont:%u, len_pacote:%u\n", erro, end_db, id_libre, status_id, id_cont, len_pacote);
	}
#endif

	return erro;
}



int gilsondb_update(const uint32_t end_db, const uint32_t id, uint8_t *data)
{
	uint32_t endereco=0, status_id=0, check_ids=0, cont_ids=0, id_libre=0, id_cont=0, len_pacote=0;
	int erro=erGILSONDB_OK;
	uint8_t b[OFF_PACK_GILSON_DB];
	header_db s_gdb;


	erro = _gilsondb_statistics(end_db, &s_gdb, &cont_ids, &id_libre, &id_cont);

	if(erro==erGILSONDB_OK)
	{
		if(id>=s_gdb.max_packs)
		{
			erro = erGILSONDB_4;
			goto deu_erro;
		}

		endereco = id * s_gdb.size_max_pack + s_gdb.size_header;

		if(endereco > (s_gdb.max_packs * s_gdb.size_max_pack))
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
		len_pacote += OFF_PACK_GILSON_DB;

		if(s_gdb.configs[eCheckUpdID]==1 || check_ids == s_gdb.check_ids)
		{
			if(s_gdb.configs[eCheckUpdID]==1)
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
		printf("DEBUG gilsondb_update::: erro:%i, end_db:%u, id:%u, status_id:%u, id_cont:%u\n", erro, end_db, id, status_id, id_cont);
	}
#endif

	return erro;
}




int gilsondb_del(const uint32_t end_db, const uint32_t id)
{
	uint32_t endereco=0, status_id, check_ids=0;
	int erro=erGILSONDB_OK;
	uint8_t b[8];
	header_db s_gdb;


	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	if(erro==erGILSONDB_OK)
	{
		if(id>=s_gdb.max_packs)
		{
			erro = erGILSONDB_7;
			goto deu_erro;
		}

		endereco = id * s_gdb.size_max_pack + s_gdb.size_header;

		if(endereco > (s_gdb.max_packs * s_gdb.size_max_pack))
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
		printf("DEBUG gilsondb_del::: erro:%i, end_db:%u, id:%u, status_id:%u\n", erro, end_db, id, status_id);
	}
#endif

	return erro;
}


int gilsondb_read(const uint32_t end_db, const uint32_t id, uint8_t *data)
{
	uint32_t endereco, crc1, crc2, check_ids=0, id_cont=0, status_id=0, len_pacote=0;
	int erro=erGILSONDB_OK;
	uint8_t valid;
	uint8_t b[OFF_PACK_GILSON_DB];
	header_db s_gdb;


	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	if(erro==erGILSONDB_OK)
	{
		if(id>=s_gdb.max_packs)
		{
			erro = erGILSONDB_9;
			goto deu_erro;
		}
		endereco = id * s_gdb.size_max_pack + s_gdb.size_header;

		if(endereco > (s_gdb.max_packs * s_gdb.size_max_pack))
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro = erGILSONDB_10;
			goto deu_erro;
		}


		// se desloca até offset do endereço do banco
		endereco += end_db;

		// análise do configs... do pacote antigo caso exista...
		erro = mem_read_buff(endereco, OFF_PACK_GILSON_DB, b);
		memcpy(&status_id, b, 4);
		memcpy(&check_ids, &b[4], 4);
		memcpy(&len_pacote, &b[8], 4);
		memcpy(&crc2, &b[12], 4);

		if(check_ids == s_gdb.check_ids)
		{
			endereco += OFF_PACK_GILSON_DB;

			// assume que o buffer 'data' vai conseguir alocar os dados resgatados!!!!
			erro = mem_read_buff(endereco, len_pacote, data);

			crc1 = gilsondb_crc(0xffffffff, data, len_pacote);

			if(crc1 == crc2)
			{
				//valid = buf_db[0];
				valid = (uint8_t)status_id&0xff;
				id_cont = (status_id>>8)&0xffffff;
				// id_cont = buf_db[1:5];  24bits
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


	deu_erro:

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG gilsondb_read::: erro:%i, end_db:%u, id:%u, valid:%u, id_cont:%u, len_pacote:%u\n", erro, end_db, id, valid, id_cont, len_pacote);
	}
#endif

	return erro;
}



int gilsondb_check(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack)
{
	uint32_t max_size=0, max2;
	int erro=erGILSONDB_OK;
	header_db s_gdb;

	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	if(erro==erGILSONDB_OK)
	{
		if(max_packs!=0 && s_gdb.max_packs != max_packs)
		{
			erro = erGILSONDB_13;
		}
		else if(offset_pack!=0 && (s_gdb.size_max_pack+OFF_PACK_GILSON_DB) != (offset_pack + OFF_PACK_GILSON_DB))
		{
			erro = erGILSONDB_14;
		}
		else if(max_packs!=0 && offset_pack!=0)
		{
			max_size = (OFF_PACK_GILSON_DB + offset_pack) * max_packs;
			max2 = s_gdb.max_packs * (s_gdb.size_max_pack+OFF_PACK_GILSON_DB);
			if(max_size > max2)  // > ou !=
			{
				erro = erGILSONDB_15;
			}
		}
	}

#if (USO_DEBUG_LIB==1)
	_print_header_db(&s_gdb);
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG gilsondb_check::: erro:%i, max_size:%u|%u\n", erro, max_size, max2);
	}
#endif

	return erro;
}



int gilsondb_get_valids(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids)
{
	uint32_t endereco, i, j=0, cont=0, status_id=0, check_ids=0;
	int erro=erGILSONDB_OK;
	uint8_t valid=0;
	uint8_t b[8];
	header_db s_gdb;

	erro = _gilsondb_check_db_init(end_db, &s_gdb);

	if(erro==erGILSONDB_OK)
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
					cont+=1;
					valids[j]=i;
					j+=1;
				}
			}
		}
	}

	*cont_ids = cont;

#if (USO_DEBUG_LIB==1)
	if(PRINT_DEBUG==1 || erro!=erGILSONDB_OK)
	{
		printf("DEBUG gilsondb_get_valids::: erro:%i\n", erro);
	}
#endif

	return erro;
}
