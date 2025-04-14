/*
 * mjm_db.h
 *
 *  Created on: 2 de abr. de 2025
 *      Author: mella
 */


#ifndef SRC_MJM_DB_MJM_DB_H_
#define SRC_MJM_DB_MJM_DB_H_

/*
Versão: 0.3 12/04/25

estrutura do "banco" que eu chamo é como se fosse uma tabela individual que é gravada em um endereço da memória fixo
e cada tabela contem um header de 32 bytes que sao a soma de 8 variaveis do tipo uint32_t. Cada tabela independe uma da outra.

estrutura do "banco" é a seguinte:
max_packs = tamanho máximo de pacotes que esse banco poderá salvar
offset_pack = tamanho de offset de cada item add no banco sendo que SEMPRE o primerio byte indica de esta ativado(1) ou desativado(0)
cont_ids = contagem crescente (para fins de banco ilimitado e que tem tratamento de sobre escrever quando chega no limite maximo)
cont_ids_aloc = total de pacotes alocados mesmo que estejam desativados
id_libre = proximo id libre
cont_mods = contagem modificacoes
code_db = código para validar criacao do banco na posicao da memoria que será gravada
max_size = limite tamanho total que esse banco acupa em bytes

limitações:
cada pacote nao pode passar de 'MAX_DATA_DB' pois temos um buffer local que aloca e monta os pacotes

regras são:
1: 	por mais que tenhamos enum 'ADD_idDB', 'UPD_idDB' e 'DEL_idDB' precimamos configurar manualmente o status_id.
2: 	'cont_ids_aloc' nao pode ser maior que 'max_packs' porem pode ser maior que 'cont_ids'.
3: 	'id_libre' identifica o proximo id que está disponível para ser gravado.
4: 	quando 'cont_ids_aloc' == 'cont_ids' == 'max_packs' significa que todos os ids estão ocupados e caso seja inserido algum dado novo
	um novo cliclo começa para 'id_libre' e ele volta para id=0 e se auto incrementa, isto é, vai escrever em cima do menor id.
	Para evitar isso, um tratamento deve ser feito manualmente e externamente.
5: 	quando for editar um id que está como excluido, automamaticamente a função organiza qual será o próximo 'id_libre' disponível

para todos os bancos a lógica será sempre os 4 primeiros bytes de cada item terao informaçoes de ativo/desativo....
cada pacote de item gravado contem:
0:4 = 4 bytes = status do id
	[0]=ativo 0=desativado, 1=ativado
	[1]=origem, 0=vem da aplicacao local, 1=vem do pc,
	[2]=reservado ???
	[3]=reservado ???
4:8 = 4 bytes = crc data
8:: = data

tem 3 funções basicas:
adiciona/insert
edita/update
exclui/delete

*/


#define OFF_INIT_DATA_DB	8  // offser que inicia a 'data' do item no banco...

// code check gravado em todos esquemas que envolve ou nao banco...
#define CODE_DB_CHECK		1654785413UL	// fixo e igual para todos!!!


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
	CODE_DB,			// check code de cada banco
	MAX_SIZE_DB			// tamanho total em bytes que o banco ocupa
};


enum e_FUN_ID_DB
{
	ADD_idDB, 		// add novo ID no banco
	UPD_idDB, 		// editar/update um ID existente
	DEL_idDB		// excluir ID do banco
};

enum e_TYPE_DATA_DB
{
	ALL_idDB,   	// para resgatar data + status_id do id
	DATA_idDB		// para resgatar somente a data do id
};

enum e_TYPE_DATA_packDB
{
	ACTIVE_idDB,   		// 0=desativado, 1=ativado
	ORIGIN_idDB,		// 0=vem da aplicacao local, 1=vem do pc, ...
	RESERV1_idDB,		// reservado1
	RESERV2_idDB,		// reservado2
};


enum e_erros_MJMDB
{
	// vai fazer 'erMJMDB_xxxx' onde xxxx-1000 = erro retornado
	erMJMDB_OK = 0,
	erMJMDB_DEL = -2000,
	erMJMDB_0,
	erMJMDB_1,
	erMJMDB_2,
	erMJMDB_3,
	erMJMDB_4,
	erMJMDB_5,
	erMJMDB_6,
	erMJMDB_7,
	erMJMDB_8,
	erMJMDB_9,
	erMJMDB_10,
	erMJMDB_11,
	erMJMDB_12,
	erMJMDB_13,
	erMJMDB_14,
	erMJMDB_15,
	erMJMDB_16,
	erMJMDB_17,
	erMJMDB_18,
	erMJMDB_19,
	erMJMDB_20,
	erMJMDB_21,
	erMJMDB_22,
	erMJMDB_23,
	erMJMDB_24,
	erMJMDB_25,
	erMJMDB_26,
	erMJMDB_27,
	erMJMDB_28,
	erMJMDB_29,
	erMJMDB_30,
	erMJMDB_31,
	erMJMDB_32,
	erMJMDB_33,
	erMJMDB_34,
	erMJMDB_35,
	erMJMDB_36,
	erMJMDB_37,
	erMJMDB_38,
	erMJMDB_39,
	erMJMDB_40,
};




//============================================================================================
//============================================================================================

int mjmdb_init(void);

// seguras...
int mjmdb_create_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack);
int mjmdb_write_db(const uint32_t end_db, const uint32_t id, const uint8_t acao, const uint32_t status_id, const uint8_t *data);
int mjmdb_read_db(const uint32_t end_db, const uint32_t id, const uint8_t flag_data, uint8_t *data);
int mjmdb_read_status_db(const uint32_t end_db, const uint32_t id, uint8_t *data);
int mjmdb_get_configs_db(const uint32_t end_db, const uint8_t tipo, uint32_t *config);
int mjmdb_get_info_db(const uint32_t end_db, char *sms, const char *nome);  // debug...
int mjmdb_get_valids_db(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids);
int mjmdb_check_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack);


// perigo...
int mjmdb_create_set_db(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint32_t cont_ids);
int mjmdb_create_set_db_libre(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint32_t cont_ids, const uint32_t libre);
int mjmdb_write_add_db(const uint32_t end_db, const uint32_t id, const uint32_t size_data, const uint32_t status_id, const uint8_t *data);
int mjmdb_read_size_db(const uint32_t end_db, const uint32_t id, const uint8_t flag_data, uint8_t *data, const uint32_t size);
int mjmdb_read_data_flex_db(const uint32_t end_db, const uint32_t id, uint8_t *data, const uint32_t size, const uint32_t start);
int mjmdb_read_data_flex_off_db(const uint32_t end_db, const uint32_t id, uint8_t *data, const uint32_t size, const uint32_t start, const uint32_t offset_data);

//============================================================================================
//============================================================================================




#endif /* SRC_MJM_DB_MJM_DB_H_ */
