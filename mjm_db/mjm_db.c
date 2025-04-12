/*
 * mjm_db.c
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

#include "mjm_db.h"


#define MAX_DATA_DB			16384  	// tamanho máximo de cada pacote gravado no banco criado, melhor ser múltiplo de 4096
#define HEADER_DB			32  	// tamanho de offset de cabeçalho de configuracoes do banco...


struct vars_header_db_fs
{
	// MANTER ESSA ORDEM!!!!! e até 32 'HEADER_DB' bytes!!!!
	uint32_t max_packs;  		// maximo de pacotes armazenados
	uint32_t offset_pack;  		// offset de cada pacote no banco
	uint32_t cont_ids;  		// contagem real de itens validos
	uint32_t cont_ids_aloc;  	// total de pacotes alocados mesmo que estejam desativados
	uint32_t id_libre;			// item libre
	uint32_t cont_mods;  		// contagem modificacoes
	uint32_t code_db;  			// código para validar criacao do banco na posicao da memoria que será gravada
	uint32_t max_size;			// limite tamanho total que esse banco acupa em bytes
	// OBS: max 8 vars de 4 bytes!!!!!!!!!!!!
};

static struct vars_header_db_fs s_header_fs;

static uint8_t buf_db[MAX_DATA_DB];  // base no setor da memoria que vale 4096 bytes, 3 setores...




// copiado da lib do littlefs "lfs_crc()"
// Software CRC implementation with small lookup table
static uint32_t mjmdb_crc(uint32_t crc, const uint8_t *buffer, const uint16_t size)
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



int mjmdb_init(void)
{
	return mem_init();
}


int mjmdb_create_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack)
{
	int erro_fs=0;
	uint8_t pack[HEADER_DB]={0};

	memset(&s_header_fs, 0x00, sizeof(s_header_fs));

	s_header_fs.max_packs = max_packs;
	s_header_fs.offset_pack = offset_pack + OFF_INIT_DATA_DB;
	s_header_fs.code_db = CODE_DB_CHECK;
	s_header_fs.max_size = HEADER_DB + (s_header_fs.offset_pack * s_header_fs.max_packs);

	if(s_header_fs.offset_pack < MAX_DATA_DB)
	{
		memcpy(pack, &s_header_fs, sizeof(s_header_fs));
		erro_fs = mem_write_buff(end_db, pack, HEADER_DB);
	}
	else
	{
		// o pacote nao pode ser alocado no buffer geral da lib 'buf_db[MAX_DATA_DB]'
		erro_fs=-666;
	}

	return erro_fs;
}

// seta valores definidos que vao ser adicionados manualmente..., para fins de importacao de dados que sabe-se quantos vai add
int mjmdb_create_set_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint32_t cont_ids)
{
	int erro_fs=0;
	uint8_t pack[HEADER_DB]={0};

	memset(&s_header_fs, 0x00, sizeof(s_header_fs));

	s_header_fs.max_packs = max_packs;
	s_header_fs.offset_pack = offset_pack + OFF_INIT_DATA_DB;
	s_header_fs.cont_ids = cont_ids;
	s_header_fs.cont_ids_aloc = cont_ids;
	s_header_fs.id_libre = cont_ids;
	s_header_fs.cont_mods = cont_ids;
	s_header_fs.code_db = CODE_DB_CHECK;
	s_header_fs.max_size = HEADER_DB + (s_header_fs.offset_pack * s_header_fs.max_packs);

	if(s_header_fs.offset_pack < MAX_DATA_DB)
	{
		// update configs banco:
		memcpy(pack, &s_header_fs, sizeof(s_header_fs));
		erro_fs = mem_write_buff(end_db, pack, HEADER_DB);
	}
	else
	{
		// o pacote nao pode ser alocado no buffer geral da lib 'buf_db[MAX_DATA_DB]'
		erro_fs=-666;
	}

	return erro_fs;
}

// seta valores definidos que vao ser adicionados manualmente..., e seta manualmente qual valor do id libre que ficará
int mjmdb_create_set_db_v2(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint32_t cont_ids, const uint32_t libre)
{
	int erro_fs=0;
	uint8_t pack[HEADER_DB]={0};

	memset(&s_header_fs, 0x00, sizeof(s_header_fs));

	s_header_fs.max_packs = max_packs;
	s_header_fs.offset_pack = offset_pack + OFF_INIT_DATA_DB;
	s_header_fs.cont_ids = cont_ids;
	s_header_fs.cont_ids_aloc = max_packs;
	s_header_fs.id_libre = libre;
	s_header_fs.cont_mods = cont_ids;
	s_header_fs.code_db = CODE_DB_CHECK;
	s_header_fs.max_size = HEADER_DB + (s_header_fs.offset_pack * s_header_fs.max_packs);

	if(s_header_fs.offset_pack < MAX_DATA_DB)
	{
		// update configs banco:
		memcpy(pack, &s_header_fs, sizeof(s_header_fs));
		erro_fs = mem_write_buff(end_db, pack, HEADER_DB);
	}
	else
	{
		// o pacote nao pode ser alocado no buffer geral da lib 'buf_db[MAX_DATA_DB]'
		erro_fs=-666;
	}

	return erro_fs;
}

// absoluta certeza de que 'id', 'size' e 'data' conferem com o banco criado!!!
// 's_header_fs' ja foi previamente configurado pois estamos em um import... normalmente
int mjmdb_write_add_db(const uint32_t end_db, const uint32_t id, const uint32_t size_data, const uint32_t status_id, const uint8_t *data)
{
	uint32_t endereco, crc;
	int erro_fs;

	// OBS: não faz verificação com 's_header_fs', perigo peridooooo

	endereco = id * (size_data+OFF_INIT_DATA_DB) + HEADER_DB;

	// update data banco:
	endereco += end_db;

	// status_id
	memcpy(buf_db, &status_id, 4);

	// calcula crc
	crc = mjmdb_crc(0xffffffff, data, size_data);
	memcpy(&buf_db[4], &crc, 4);

	// aloca a data pack
	if((size_data+OFF_INIT_DATA_DB) <= MAX_DATA_DB)
	{
		memcpy(&buf_db[OFF_INIT_DATA_DB], data, size_data);

		erro_fs = mem_write_buff(endereco, buf_db, size_data+OFF_INIT_DATA_DB);
	}
	else
	{
		erro_fs=-666;
	}

	return erro_fs;
}

int mjmdb_write_db(const uint32_t end_db, const uint32_t id, const uint32_t acao, const uint32_t status_id, const uint8_t *data)
{
	int erro_fs;
	uint32_t endereco=0, i, endereco2, crc;
	uint8_t valid=0;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));

		if(acao==ADD_idDB)  // ADICIONAR
		{
			// risco de explosao:
			// chegou no limite do banco e mesmo assim quer add um novo item no banco
			// entra em acao o esquema de apagar o mais antigo sendo que isso até entao é simplesmente
			// voltar para posicao 0 e começar a add novamente
			if(s_header_fs.cont_ids>=s_header_fs.max_packs && s_header_fs.cont_ids_aloc>=s_header_fs.max_packs)
			{
				if(s_header_fs.id_libre>=s_header_fs.max_packs)
				{
					s_header_fs.id_libre=0;
				}
				endereco = s_header_fs.id_libre * s_header_fs.offset_pack + HEADER_DB;
				s_header_fs.id_libre+=1;  // sera o proximo libre
				// nao vai mais modificar 'cont_ids', 'cont_ids_aloc' a nao ser que seja via outra funcao
			}
			else
			{
				endereco = s_header_fs.id_libre * s_header_fs.offset_pack + HEADER_DB;
				if(s_header_fs.id_libre==s_header_fs.cont_ids)  // indica que nao temos libre ta só add em modo crescente
				{
					s_header_fs.id_libre+=1;
					if(s_header_fs.cont_ids_aloc<=s_header_fs.cont_ids)  // 'cont_ids' nunca vai passar o 'cont_ids_aloc'
					{
						s_header_fs.cont_ids_aloc+=1;  // nao posso ficar somando nessa condicao quando o ultimo excluido foi a ultima posicao
					}
				}
				else  // indica que temos libres, entao temos que usar o libre
				{
					// tem que descobrir qual o proximo libre
					for(i=0; i<s_header_fs.cont_ids_aloc; i++)
					{
						endereco2 = (i*s_header_fs.offset_pack+HEADER_DB)+end_db;
						valid = mem_read_uint8(endereco2);
						if(valid==0 && i!=s_header_fs.id_libre)
						{
							break;
						}
					}
					if(i==s_header_fs.cont_ids_aloc)  // será o maximo
					{
						s_header_fs.id_libre=s_header_fs.cont_ids+1;
					}
					else
					{
						s_header_fs.id_libre = i;
					}
				}
				s_header_fs.cont_ids+=1;
			}
		}
		else if(acao==UPD_idDB)  // EDITAR
		{
			if(id>=s_header_fs.max_packs)
			{
				erro_fs = -1;
				goto deu_erro;
			}
			endereco = id * s_header_fs.offset_pack + HEADER_DB;
		}
		else if(acao==DEL_idDB)  // EXCLUIR
		{
			if(id>=s_header_fs.max_packs)
			{
				erro_fs = -2;
				goto deu_erro;
			}
			endereco = id * s_header_fs.offset_pack + HEADER_DB;
			s_header_fs.id_libre = id;  // nao vai ser assim..., massss aqui define o atual libre como sendo o último do excluiu
			if(s_header_fs.cont_ids) s_header_fs.cont_ids-=1;
		}
		else
		{
			erro_fs = -3;
			goto deu_erro;
		}

		if(endereco > s_header_fs.max_size)
		{
			// 'endereco' ainda é um valor partindo de zero, nao está somado o offset de 'end_db' logo podemos
			// medir o alcance em bytes que vamos querer gravar ou ler...
			erro_fs = -4;
			goto deu_erro;
		}


		s_header_fs.cont_mods+=1;  // contagem de modificaçoes realizadas

		// se desloca até offset do endereço do banco
		endereco += end_db;

		// status_id
		memcpy(buf_db, &status_id, 4);

		// calcula crc
		crc = mjmdb_crc(0xffffffff, data, s_header_fs.offset_pack-OFF_INIT_DATA_DB);
		memcpy(&buf_db[4], &crc, 4);

		// aloca a data pack
		memcpy(&buf_db[OFF_INIT_DATA_DB], data, s_header_fs.offset_pack-OFF_INIT_DATA_DB);

		//printf("endereco write:%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_header_fs.offset_pack);
		erro_fs = mem_write_buff(endereco, buf_db, s_header_fs.offset_pack);

		// update configs banco:
		memcpy(pack, &s_header_fs, sizeof(s_header_fs));
		erro_fs = mem_write_buff(end_db, pack, sizeof(s_header_fs));

		// OBS: pior caso é quando a 'data' esta no mesmo setor do 's_header_fs' e ai vai apagrar e gravar duas vezes!!!!
		// otimizar isso!!!!!!!!!!!!!!!!!!

		/*
		// valida gravacao???
		memset(buf_db, 0x00, s_header_fs.offset_pack);
		mem_read_buff(endereco, s_header_fs.offset_pack, buf_db);
		printf("endereco read :%u status_id:%08x (%02x %02x %02x %02x) offset_pack:%u\n", endereco, status_id, buf_db[3], buf_db[2], buf_db[1], buf_db[0], s_header_fs.offset_pack);
		*/
	}

	deu_erro:

	return erro_fs;
}


int mjmdb_read_db(const uint32_t end_db, const uint32_t id, const uint8_t flag_data, uint8_t *data)
{
	int erro_fs;
	uint32_t endereco, crc1, crc2;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));

		if(id>=s_header_fs.max_packs)
		{
			erro_fs = -1;
			goto deu_erro;
		}
		endereco = id * s_header_fs.offset_pack + HEADER_DB;
		endereco += end_db;

		// isso ja foi verificado quando cria o banco mas vamos fazer pra garantir...
		if(s_header_fs.offset_pack < MAX_DATA_DB)
		{
			//printf("endereco read:%u\n", endereco);
			erro_fs = mem_read_buff(endereco, s_header_fs.offset_pack, buf_db);
			crc1 = mjmdb_crc(0xffffffff, &buf_db[OFF_INIT_DATA_DB], s_header_fs.offset_pack-OFF_INIT_DATA_DB);
			memcpy(&crc2, &buf_db[4], 4);

			if(crc1==crc2)
			{
				endereco = 0;
				if(flag_data)  // somente a data sera alocada em "*data"
				{
					endereco += OFF_INIT_DATA_DB;  // pula o 'status_id' e 'crc'
					s_header_fs.offset_pack -= OFF_INIT_DATA_DB;
				}
				// se assume que '*data' consegue alocar o tamanho de 's_header_fs.offset_pack' normalmente
				memcpy(data, &buf_db[endereco], s_header_fs.offset_pack);
			}
			else
			{
				erro_fs=-66;
			}
		}
		else
		{
			erro_fs=-67;
		}
	}

	deu_erro:

	return erro_fs;
}

// ter certeza da tamanho que deseja ler do pacote do id, normalmente flag_data==1 pois queremos ler um tamanho específico do pacote
int mjmdb_read_size_db(const uint32_t end_db, const uint32_t id, const uint8_t flag_data, uint8_t *data, const uint32_t size)
{
	int erro_fs;
	uint32_t endereco;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));

		if(id>=s_header_fs.max_packs)
		{
			erro_fs = -1;
			goto deu_erro;
		}

		endereco = id * s_header_fs.offset_pack + HEADER_DB;
		endereco += end_db;

		if(flag_data == DATA_idDB)  // somente a data sera alocada em "*data"
		{
			endereco += OFF_INIT_DATA_DB;  // pula o 'status_id' e 'crc'
			//s_header_fs.offset_pack -= OFF_INIT_DATA_DB;
		}

		// o mínimo que vamos validar é limitar com total máximo de bytes do banco
		// quanto a '*data' to nem ai se vai suportar o que vamos alocar... é por conta e risco de quem chamar essa função
		if(size < s_header_fs.max_size)
		{
			erro_fs = mem_read_buff(endereco, size, data);

			// fazer verificacao de crc???? para dizer se o que leu faz sentido???
			// ...
			// meio que nao tem como pois 'size' é dinâmico... mas daria atraves do 'id' ler o pacote correto e valida-lo
		}
		else
		{
			erro_fs = -2;
		}
	}

	deu_erro:

	return erro_fs;
}

// ter certeza da tamanho que deseja ler do pacote do id, e dos offsets que se deseja alocar
int mjmdb_read_data_flex_db(const uint32_t end_db, const uint32_t id, uint8_t *data, const uint32_t size, const uint32_t start)
{
	int erro_fs;
	uint32_t endereco;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));

		if(id>=s_header_fs.max_packs)
		{
			erro_fs = -1;
			goto deu_erro;
		}

		endereco = id * s_header_fs.offset_pack + HEADER_DB;

		// somente a data sera alocada em "*data"
		endereco += OFF_INIT_DATA_DB;  // pula o 'status_id' e 'crc'

		endereco += start;  // inicia em que ponto da "data" vai iniciar a quantidade 'size' desejada

		// o mínimo que vamos validar é limitar com total máximo de bytes do banco
		// quanto a '*data' to nem ai se vai suportar o que vamos alocar... é por conta e risco de quem chamar essa função
		if(size < s_header_fs.max_size && endereco < s_header_fs.max_size)
		{
			endereco += end_db;
			erro_fs = mem_read_buff(endereco, size, data);
		}
		else
		{
			erro_fs = -2;
		}

		// fazer verificacao de crc???? para dizer se o que leu faz sentido???
		// ...
		// quebra o brique pq nao leio a data completa para calcular o crc geral...
	}

	deu_erro:

	return erro_fs;
}

// estilo a 'mjmdb_write_add_db' que devemos passar o valor de offset de data
int mjmdb_read_data_flex_off_db(const uint32_t end_db, const uint32_t id, uint8_t *data, const uint32_t size, const uint32_t start, const uint32_t offset_data)
{
	int erro_fs;
	uint32_t endereco;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));

		if(id>=s_header_fs.max_packs)
		{
			erro_fs = -1;
			goto deu_erro;
		}

		endereco = id * (offset_data+OFF_INIT_DATA_DB) + HEADER_DB;

		endereco += start;  // inicia em que ponto da "data"???

		// o mínimo que vamos validar é limitar com total máximo de bytes do banco
		// quanto a '*data' to nem ai se vai suportar o que vamos alocar... é por conta e risco de quem chamar essa função
		if(size < s_header_fs.max_size && endereco < s_header_fs.max_size)
		{
			endereco += end_db;
			erro_fs = mem_read_buff(endereco, size, data);

			// fazer verificacao de crc???? para dizer se o que leu faz sentido???
			// ...
			// quebra o brique pq nao leio a data completa para calcular o crc geral...
		}
		else
		{
			erro_fs = -2;
		}
	}

	deu_erro:

	return erro_fs;
}

int mjmdb_read_status_db(const uint32_t end_db, const uint32_t id, uint8_t *data)
{
	int erro_fs;
	uint32_t endereco;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));

		endereco = id * s_header_fs.offset_pack + HEADER_DB;
		endereco += end_db;
		erro_fs = mem_read_buff(endereco, 4, data);  // somente os 4 primeiros bytes do item!!! chamado de 'status_id' quando grava
	}

	return erro_fs;
}

int mjmdb_get_configs_db(const uint32_t end_db, const uint8_t tipo, uint32_t *config)
{
	int erro_fs;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));

		if(tipo==CONT_IDS_DB)
		{
			*config = s_header_fs.cont_ids;
		}
		else if(tipo==CONT_IDS_ALOC_DB)
		{
			*config = s_header_fs.cont_ids_aloc;
		}
		else if(tipo==ID_LIBRE_DB)
		{
			*config = s_header_fs.id_libre;
		}
		else if(tipo==CONT_MODS_DB)
		{
			*config = s_header_fs.cont_mods;
		}
		else if(tipo==MAX_IDS_DB)
		{
			*config = s_header_fs.max_packs;
		}
		else if(tipo==OFF_IDS_DB)
		{
			*config = s_header_fs.offset_pack-OFF_INIT_DATA_DB;  // remove parte "implicita" de 'OFF_INIT_DATA_DB'!!!
		}
		else if(tipo==CODE_DB)
		{
			*config = s_header_fs.code_db;
		}
		else if(tipo==MAX_SIZE_DB)
		{
			*config = s_header_fs.max_size;
		}
		else
		{
			*config = 0;
		}
	}
	return erro_fs;
}


int mjmdb_get_info_db(const uint32_t end_db, char *sms, const char *nome)  // debug...
{
	int erro_fs, i=0;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));
		i = sprintf(sms, "\nBANCO:%s, END:%u, SETOR:%u\n"
				"\tMAX_PACKS:%u, OFFSET_PACK:%u CODE:%u\n"
				"\tcont:%u, libre:%u, aloc:%u, mods:%u, max_size:%u\n\n",
				nome, end_db, (end_db/4096), s_header_fs.max_packs,
				s_header_fs.offset_pack, s_header_fs.code_db,
				s_header_fs.cont_ids, s_header_fs.id_libre,
				s_header_fs.cont_ids_aloc, s_header_fs.cont_mods, s_header_fs.max_size);
	}
	return i;
}



int mjmdb_get_valids_db(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids)
{
	int erro_fs;
	uint8_t valid=0;
	uint32_t endereco, i, j=0, cont=0;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));
		//printf("s_header_fs.cont_ids_aloc:%u\n", s_header_fs.cont_ids_aloc);
		for(i=0; i<s_header_fs.cont_ids_aloc; i++)
		{
			endereco = (i*s_header_fs.offset_pack+HEADER_DB);
			endereco += end_db;
			valid = mem_read_uint8(endereco);
			//printf("valid:%u\n", valid);
			if(valid)  // 'valid==1' cuidar pois pode haver 0 ou 255 indica que está vazio...
			{
				cont+=1;
				valids[j]=i;
				j+=1;
			}
		}

		*cont_ids = cont;
		// moral é que 'cont' tem que ser igual a 's_header_fs.cont_ids'
		//*id_libre = s_header_fs.id_libre;
	}

	return erro_fs;
}


int mjmdb_check_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack)
{
	int erro_fs;
	uint8_t pack[HEADER_DB];

	erro_fs = mem_read_buff(end_db, HEADER_DB, pack);

	if(erro_fs==0)
	{
		memcpy(&s_header_fs, pack, sizeof(s_header_fs));

		if(s_header_fs.code_db != CODE_DB_CHECK)
		{
			erro_fs = -444;
		}
		else if(max_packs!=0 && s_header_fs.max_packs != max_packs)
		{
			erro_fs = -445;
		}
		else if(offset_pack!=0 && s_header_fs.offset_pack != (offset_pack + OFF_INIT_DATA_DB))
		{
			erro_fs = -446;
		}
	}

	return erro_fs;
}

