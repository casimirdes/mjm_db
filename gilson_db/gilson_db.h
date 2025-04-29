/*
 * gilson_db.h
 *
 *  Created on: 2 de abr. de 2025
 *      Author: mella
 */

#ifndef SRC_GILSON_DB_GILSON_DB_H_
#define SRC_GILSON_DB_GILSON_DB_H_

/*

Versão: 0.1 15/04/25

100% baseado no "neide_db" e "gilson"

gilson_db = banco de dados com giison
- no estilo de tabelas
- com colunas prefixadas no formato gilson
- limite de 256 colunas
- limite de string de 256 caracteres (utf-8)
- tem um tamanho de linhas predefinido
- controle de status do banco, validação e tratamento de erros em todas as operações
- toda linha terá um CRC e um identificador auto incremental
- header só modifica uma vez quando é criado a tabela
- é possível excluir/deletar um id/linha porem mantem o id original mas inativa a linha
- é possivel ativar a linha novamente e entao edita-la para reutilizar
- os pacotes de gilson sao salvos no modo 'GSON_MODO_ZIP' que é o mais compacto
- OFF_PACK_GILSON_DB=16  // status_id + check_ids + tamanho do pacote + crc (as 4 do tipo uint32_t)


- inicialmente sem suporte a mudança/alteração de fromato de tabela ja criada
- cada 'nome de tabela' é um endereço de onde vai ser o ponto inicial na memória logo sempre vai gravar no mesma posição
- não tem tratamento de nivelamento de desgaste de setores, isto é, não escreve dinamicamente em multi setores


 */

#define OFF_PACK_GILSON_DB	16  // status_id + check_ids + tamanho do pacote + crc (as 4 do tipo uint32_t)



/*
// controle interno dos contadores do banco (cada um refere a um uint32_t do config banco), nao confundir com os 4 bytes init de cada item que indicam status do 'id'
// segue a ordem da struct 's_config_fs'!!!!
enum e_status_gilsondb
{
	eMAX_IDS_DB, 		// maximo de pacotes armazenados
	eOFF_IDS_DB,		// offset de cada pacote no banco (descontado 'OFF_INIT_DATA_DB')
	eCODE_DB,			// check code de cada banco
	eMAX_SIZE_DB,		// tamanho total em bytes que o banco ocupa
	eCHECK_IDS_DB,		// validador de pacotes com header
	eCONT_IDS_DB, 		// contagem real de itens validos
	eID_LIBRE_DB, 		// item libre
	eID_CONT_DB,		// id auto cont max
};
*/


/*
	GSON_SINGLE,  // valor unico
	GSON_LIST,  // é no formato lista, [u16] mas até 64k
	GSON_MTX2D,  // é no formato matriz, max 2 dimenções!!! [u8][u16] mas jamais pode passar de 64k!!!!


	GSON_tBIT,
	GSON_tINT8,
	GSON_tUINT8,
	GSON_tINT16,
	GSON_tUINT16,
	GSON_tINT32,
	GSON_tUINT32,
	GSON_tINT64,
	GSON_tUINT64,
	GSON_tFLOAT32,
	GSON_tFLOAT64,
	GSON_tSTRING,
*/

/*
enum e_TIPOS_GILSONDB
{
	GDB_SINGLE_INT8,
	GDB_SINGLE_UINT8,
	GDB_SINGLE_INT16,
	GDB_SINGLE_UINT16,
	GDB_SINGLE_INT32,
	GDB_SINGLE_UINT32,
	GDB_SINGLE_INT64,
	GDB_SINGLE_UINT64,
	GDB_SINGLE_FLOAT32,
	GDB_SINGLE_FLOAT64,
	GDB_SINGLE_STRING,

	GDB_LIST_INT8,
	GDB_LIST_UINT8,
	GDB_LIST_INT16,
	GDB_LIST_UINT16,
	GDB_LIST_INT32,
	GDB_LIST_UINT32,
	GDB_LIST_INT64,
	GDB_LIST_UINT64,
	GDB_LIST_FLOAT32,
	GDB_LIST_FLOAT64,
	GDB_LIST_STRING,

	GDB_MTX2D_INT8,
	GDB_MTX2D_UINT8,
	GDB_MTX2D_INT16,
	GDB_MTX2D_UINT16,
	GDB_MTX2D_INT32,
	GDB_MTX2D_UINT32,
	GDB_MTX2D_INT64,
	GDB_MTX2D_UINT64,
	GDB_MTX2D_FLOAT32,
	GDB_MTX2D_FLOAT64,
	//GDB_MTX2D_STRING,		// ???? nada ainda...  pois uma lista de strings seria uma matriz de string???

};
*/


enum e_erros_GILSONDB
{
	// vai fazer 'erGILSONDB_xxxx' onde xxxx-1000 = erro retornado
	erGILSONDB_OK = 0,
	erGILSONDB_DEL = -4000,
	erGILSONDB_LOT,
	erGILSONDB_OCUPADO,
	erGILSONDB_0,
	erGILSONDB_1,
	erGILSONDB_2,
	erGILSONDB_3,
	erGILSONDB_4,
	erGILSONDB_5,
	erGILSONDB_6,
	erGILSONDB_7,
	erGILSONDB_8,
	erGILSONDB_9,
	erGILSONDB_10,
	erGILSONDB_11,
	erGILSONDB_12,
	erGILSONDB_13,
	erGILSONDB_14,
	erGILSONDB_15,
	erGILSONDB_16,
	erGILSONDB_17,
	erGILSONDB_18,
	erGILSONDB_19,
	erGILSONDB_20,
	erGILSONDB_21,
	erGILSONDB_22,
	erGILSONDB_23,
	erGILSONDB_24,
	erGILSONDB_25,
	erGILSONDB_26,
	erGILSONDB_27,
	erGILSONDB_28,
	erGILSONDB_29,
	erGILSONDB_30,
	erGILSONDB_31,
	erGILSONDB_32,
	erGILSONDB_33,
	erGILSONDB_34,
	erGILSONDB_35,
	erGILSONDB_36,
	erGILSONDB_37,
	erGILSONDB_38,
	erGILSONDB_39,
	erGILSONDB_40,
};




//============================================================================================
//============================================================================================

int gilsondb_init(void);


//int gilsondb_create(const uint32_t end_db, const uint32_t max_packs, const uint8_t *settings, const uint8_t max_keys, const uint8_t *keys);  // **keys
int gilsondb_create_init(const uint32_t end_db, const uint32_t max_packs, const uint8_t *settings);
int gilsondb_create_add(const uint8_t key, const uint8_t tipo1, const uint8_t tipo2, const uint16_t cont_list_a, const uint16_t cont_list_b, const uint16_t cont_list_step);
int gilsondb_create_end(const uint32_t end_db);


int gilsondb_read(const uint32_t end_db, const uint32_t id, uint8_t *data);
int gilsondb_add(const uint32_t end_db, uint8_t *data);
int gilsondb_update(const uint32_t end_db, const uint32_t id, uint8_t *data);
int gilsondb_del(const uint32_t end_db, const uint32_t id);


int gilsondb_check(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack);
int gilsondb_get_valids(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids);



/*
int gilsondb_get_configs(const uint32_t end_db, const uint8_t tipo, uint32_t *config);
int gilsondb_get_info(const uint32_t end_db, char *sms, const char *nome);  // debug...


int gilsondb_read(const uint32_t end_db, const uint32_t id, uint8_t *data);
int gilsondb_add(const uint32_t end_db, const uint8_t *data);
int gilsondb_update(const uint32_t end_db, const uint32_t id, uint8_t *data);
int gilsondb_del(const uint32_t end_db, const uint32_t id);
*/




//============================================================================================
//============================================================================================


#endif /* SRC_GILSON_DB_GILSON_DB_H_ */
