/*
 * gilson_db.h
 *
 *  Created on: 2 de abr. de 2025
 *      Author: mella
 */

#ifndef SRC_GILSON_DB_GILSON_DB_H_
#define SRC_GILSON_DB_GILSON_DB_H_

/*

Versão: 0.41 26/05/25

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


// controle interno dos contadores do banco (cada um refere a um uint32_t do config banco), nao confundir com os 4 bytes init de cada item que indicam status do 'id'
// segue a ordem da struct 's_config_fs'!!!!
enum e_status_gilsondb
{
	egMAX_IDS_DB, 		// maximo de pacotes armazenados
	egOFF_IDS_DB,		// offset de cada pacote no banco (descontado 'OFF_INIT_DATA_DB')
	egCODE_DB,			// check code de cada banco
	egMAX_SIZE_DB,		// tamanho total em bytes que o banco ocupa
	egCHECK_IDS_DB,		// validador de pacotes com header
	egCONT_IDS_DB, 		// contagem real de itens validos
	egID_LIBRE_DB, 		// item libre
	egID_CONT_DB,		// id auto cont max
	egUSED_BYTES_DB,	// quanto do banco foi utilizado em bytes nesse momento
	egFREE_BYTES_DB,	// quanto tem libre em bytes nesse momento
};


enum e_config_gilsondb
{
	egFixedSize,		// 0=ativado cada pack de id é gravado com offset do pior caso para poder termos add/update/delete como um banco normal, 1=somente add data dinamica e invativar (invalida qualquer outro flag!)
	egAutoLoop, 		// 1=flag de configuração do auto_loop, nao retorna o erGILSONDB_LOT e add sempre no local do mais antigo 'id_cont'
	egCheckUpdID,		// 1=flag validar ou nao 'check_ids' na funcao de update id, temos um code de banco antigo ou lixo qualquer... vai add mesmo assim e assumir esse local como valido
	egCheckAddID,		// 1=flag add no lugar de inativo e válido nao muda 'id_cont', para fins de manter historico de 'id_cont' caso ja tenha um antigo

	egLENMAX			// final, até 32!!!!
};


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
	erGILSONDB_LOT,  // banco lotado/cheio
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
	erGILSONDB_41,
	erGILSONDB_42,
	erGILSONDB_43,
	erGILSONDB_44,
	erGILSONDB_45,
	erGILSONDB_46,
	erGILSONDB_47,
	erGILSONDB_48,
	erGILSONDB_49,
	erGILSONDB_50,
};




//============================================================================================
//============================================================================================

int32_t gilsondb_init(void);


int32_t gilsondb_create_init(const uint32_t end_db, const uint32_t max_packs, const uint32_t codedb, const uint32_t max_bytes, const uint8_t *settings);
int32_t gilsondb_create_add(const uint8_t key, const uint8_t tipo1, const uint8_t tipo2, const uint16_t cont_list_a, const uint16_t cont_list_b, const uint16_t cont_list_step);
int32_t gilsondb_create_add_map(const uint16_t *map);
int32_t gilsondb_create_end(const uint32_t end_db);


int32_t gilsondb_add(const uint32_t end_db, uint8_t *data);
int32_t gilsondb_update(const uint32_t end_db, const uint32_t id, uint8_t *data);
int32_t gilsondb_del(const uint32_t end_db, const uint32_t id);
int32_t gilsondb_read(const uint32_t end_db, const uint32_t id, uint8_t *data);


int32_t gilsondb_check(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint32_t codedb);
int32_t gilsondb_get_valids(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids);
int32_t gilsondb_get_configs(const uint32_t end_db, const uint8_t tipo, uint32_t *config);
int32_t gilsondb_get_info(const uint32_t end_db, char *sms, const char *nome);


int gilsondb_info_deep(const uint32_t end_db, const char *nome_banco);


int32_t gilsondb_encode_init(uint8_t *pack, const uint16_t size_max_pack);
int32_t gilsondb_encode_end(uint32_t *crc);
int32_t gilsondb_encode_mapfix(const uint16_t *map, const uint8_t *valor);
int32_t gilsondb_encode_mapdin(const uint16_t *map, ...);

int32_t gilsondb_decode_init(const uint8_t *pack);
int32_t gilsondb_decode_end(uint32_t *crc);
int32_t gilsondb_decode_mapfix(const uint16_t *map, uint8_t *valor);
int32_t gilsondb_decode_mapdin(const uint16_t *map, ...);

// para multi bancos no mesmo endereço via 'map'
int32_t gilsondb_create_multi_init(const uint32_t end_db, const uint32_t max_packs, const uint32_t codedb, const uint32_t max_bytes, const uint8_t n_bancos, const uint8_t *settings);
int32_t gilsondb_create_multi_add_map(const uint8_t i_banco, const uint8_t n_chaves, const uint16_t map[][6]);
int32_t gilsondb_create_multi_end(const uint32_t end_db);

int32_t gilsondb_multi_add(const uint32_t end_db, const uint8_t i_banco, uint8_t *data);

int32_t gilsondb_get_multi_valids(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids, uint8_t *i_bancos);
//============================================================================================
//============================================================================================


#endif /* SRC_GILSON_DB_GILSON_DB_H_ */
