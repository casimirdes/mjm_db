/*
 * mjm_db.h
 *
 *  Created on: 2 de abr. de 2025
 *      Author: mella
 */

#ifndef SRC_MJM_DB_MJM_DB_H_
#define SRC_MJM_DB_MJM_DB_H_

/*
Versão: 0.1 02/04/25

estrutura do "banco" que eu chamo é como se fosse uma tabela individueal que é gravada em um endereço da memória fixo
e cada tabela é contem um header de 32 que sao a soma de 8 variaveis do tipo uint32_t, são elas:

estrutura do "banco" é a seguinte:
max_packs = tamanho máximo de pacotes que esse banco poderá salvar
offset_pack = tamanho de offset de cada item add no banco sendo que SEMPRE o primerio byte indica de esta ativado(1) ou desativado(0)
cont_ids = contagem crescente (para fins de banco ilimitado e que tem tratamento de sobre escrever quando chega no limite maximo)
cont_ids_aloc = total de pacotes alocados mesmo que estejam desativados
id_libre = proximo id libre
cont_mods = contagem modificacoes
code_db = código para validar criacao do banco na posicao da memoria que será gravada

para todos os bancos a lógica será sempre os 4 primeiros bytes de cada item terao informaçoes de ativo/desativo....
cada pacote de item gravado contem:
0:4 = 4 bytes = status do id
	[0]=ativo 0=desativado, 1=ativado
	[1]=origem, 0=vem da aplicacao local, 1=vem do pc,
	[2]=reservado ???
	[3]=reservado ???
4:8 = 4 bytes = crc data
8:: = data
*/

//#define OFF_INIT_ID_DB		4  // (uint32_t) para todo item no banco os 4 primeiros bytes sao "status/controle", todos offsets_packs ja tem q somar isso junto
//#define OFF_CRC_ID_DB		8  // (uint32_t) para cada item calcula um crc logo a data vai sempre começar no index 12
#define OFF_INIT_DATA_DB	8  // offser que inicia a 'data' do item no banco...

// code check gravado em todos esquemas que envolve ou nao banco...
#define CODE_DB_CHECK		1654785413UL	// fixo e igual para todos!!!


enum e_FUN_ID_DB
{
	ADD_ID_DB, 		// add novo no banco
	EDI_ID_DB, 		// editar um existente
	DEL_ID_DB		// excluir do banco
};

// controle interno dos contadores do banco (cada um refere a um uint32_t do config banco), nao confundir com os 4 bytes init de cada item que indicam status do 'id'
// segue a ordem da struct 's_config_fs'!!!!
enum e_TYPE_STATUS_DB
{
	CONT_IDS_DB, 		// contagem real de itens validos
	CONT_IDS_ALOC_DB, 	// total de pacotes alocados mesmo que estejam desativados
	ID_LIBRE_DB, 		// item libre
	CONT_MODS_DB, 		// contagem modificacoes
	MAX_IDS_DB, 		// maximo de pacotes armazenados
	OFF_IDS_DB,			// offset de cada pacote no banco (descontado 'OFF_INIT_DATA_DB')
	CODE_DB				// check code de cada banco
};


enum e_TYPE_DATA_DB
{
	ALL_DB,   	// para resgatar data + status_id do id
	DATA_DB		// para resgatar somente a data do id
};



//============================================================================================
//============================================================================================

int mjmdb_init(void);

// seguras...
int mjmdb_create_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack);
int mjmdb_write_db(const uint32_t end_db, const uint32_t id, const uint32_t acao, const uint32_t status_id, const uint8_t *data);
int mjmdb_read_db(const uint32_t end_db, const uint32_t id, const uint8_t flag_data, uint8_t *data);
int mjmdb_read_status_db(const uint32_t end_db, const uint32_t id, uint8_t *data);
int mjmdb_get_configs_db(const uint32_t end_db, const uint8_t tipo, uint32_t *config);
int mjmdb_get_info_db(const uint32_t end_db, char *sms, const char *nome);  // debug...
int mjmdb_get_valids_db(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids);
int mjmdb_check_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack);


// perigo...
int mjmdb_create_set_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint32_t cont_ids);
int mjmdb_create_set_db_v2(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint32_t cont_ids, const uint32_t libre);
int mjmdb_write_add_db(const uint32_t end_db, const uint32_t id, const uint32_t size_data, const uint32_t status_id, const uint8_t *data);
int mjmdb_read_size_db(const uint32_t end_db, const uint32_t id, const uint8_t flag_data, uint8_t *data, const uint32_t size);
int mjmdb_read_data_flex_db(const uint32_t end_db, const uint32_t id, uint8_t *data, const uint32_t size, const uint32_t start);
int mjmdb_read_data_flex_off_db(const uint32_t end_db, const uint32_t id, uint8_t *data, const uint32_t size, const uint32_t start, const uint32_t offset_data);

//============================================================================================
//============================================================================================




#endif /* SRC_MJM_DB_MJM_DB_H_ */
