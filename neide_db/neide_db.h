/*
 * neide_db.h
 *
 *  Created on: 12 de abr. de 2025
 *      Author: mella
 */

#ifndef SRC_NEIDE_DB_NEIDE_DB_H_
#define SRC_NEIDE_DB_NEIDE_DB_H_

/*
Versão: 0.11 14/04/25

100% baseado no "mjm_db"

estrutura do "banco" que eu chamo é como se fosse uma tabela individual que é gravada em um endereço da memória fixo
e cada tabela contem um header de 32 bytes que sao a soma de 8 variaveis do tipo uint32_t. Cada tabela independe uma da outra.

estrutura do "banco" é a seguinte:
max_packs = tamanho máximo de pacotes que esse banco poderá salvar
offset_pack = tamanho de offset de cada item add no banco sendo que SEMPRE o primerio byte indica de esta ativado(1) ou desativado(0)
code_db = código para validar criacao do banco na posicao da memoria que será gravada
max_size = limite tamanho total que esse banco acupa em bytes

Calculados em cada chamada...
cont_ids = contagem crescente (para fins de banco ilimitado e que tem tratamento de sobre escrever quando chega no limite maximo)
id_libre = proximo id libre


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
	[1:4]=24 bits tipo de id auto incroment, para fins de ver qual foi o último e o primeiro gravado
4:8 = 4 bytes = crc data
8:: = data

tem 3 funções basicas:
adiciona/insert
edita/update
exclui/delete

*/

// controle interno dos contadores do banco (cada um refere a um uint32_t do config banco), nao confundir com os 4 bytes init de cada item que indicam status do 'id'
// segue a ordem da struct 's_config_fs'!!!!
enum e_status_neidedb
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


enum e_erros_NEIDEDB
{
	// vai fazer 'erNEIDEDB_xxxx' onde xxxx-1000 = erro retornado
	erNEIDEDB_OK = 0,
	erNEIDEDB_DEL = -3000,
	erNEIDEDB_LOT,
	erNEIDEDB_0,
	erNEIDEDB_1,
	erNEIDEDB_2,
	erNEIDEDB_3,
	erNEIDEDB_4,
	erNEIDEDB_5,
	erNEIDEDB_6,
	erNEIDEDB_7,
	erNEIDEDB_8,
	erNEIDEDB_9,
	erNEIDEDB_10,
	erNEIDEDB_11,
	erNEIDEDB_12,
	erNEIDEDB_13,
	erNEIDEDB_14,
	erNEIDEDB_15,
	erNEIDEDB_16,
	erNEIDEDB_17,
	erNEIDEDB_18,
	erNEIDEDB_19,
	erNEIDEDB_20,
	erNEIDEDB_21,
	erNEIDEDB_22,
	erNEIDEDB_23,
	erNEIDEDB_24,
	erNEIDEDB_25,
	erNEIDEDB_26,
	erNEIDEDB_27,
	erNEIDEDB_28,
	erNEIDEDB_29,
	erNEIDEDB_30,
	erNEIDEDB_31,
	erNEIDEDB_32,
	erNEIDEDB_33,
	erNEIDEDB_34,
	erNEIDEDB_35,
	erNEIDEDB_36,
	erNEIDEDB_37,
	erNEIDEDB_38,
	erNEIDEDB_39,
	erNEIDEDB_40,
};




//============================================================================================
//============================================================================================

int neidedb_init(void);

int neidedb_create(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack, const uint8_t auto_loop, const uint8_t check_update_id, const uint8_t check_add_id_inativo);
int neidedb_check(const uint32_t end_db, const uint32_t max_packs, const uint32_t offset_pack);
int neidedb_get_configs(const uint32_t end_db, const uint8_t tipo, uint32_t *config);
int neidedb_get_valids(const uint32_t end_db, uint32_t *cont_ids, uint16_t *valids);

int neidedb_get_info(const uint32_t end_db, char *sms, const char *nome);  // debug...
int neidedb_info_deep(const uint32_t end_db, const char *nome_banco);  // debug...

int neidedb_read(const uint32_t end_db, const uint32_t id, uint8_t *data);
int neidedb_add(const uint32_t end_db, const uint8_t *data);
int neidedb_update(const uint32_t end_db, const uint32_t id, uint8_t *data);
int neidedb_del(const uint32_t end_db, const uint32_t id);


//============================================================================================
//============================================================================================



#endif /* SRC_NEIDE_DB_NEIDE_DB_H_ */
