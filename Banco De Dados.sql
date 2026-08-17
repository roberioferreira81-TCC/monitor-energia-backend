-- ============================================================
-- Schema do banco de dados - Sistema de Monitoramento de Energia
-- ============================================================

CREATE DATABASE IF NOT EXISTS monitor_energia
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE monitor_energia;

-- ------------------------------------------------------------
-- Tabela de leituras: cada linha é um "envio" do ESP32
-- energia_kwh = energia consumida DESDE O ÚLTIMO ENVIO (delta),
-- não é acumulado total. Isso permite somar por período (SUM)
-- sem depender de contador que pode zerar se o ESP32 reiniciar.
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS leituras (
    id                  INT AUTO_INCREMENT PRIMARY KEY,
    dispositivo_id      VARCHAR(50) NOT NULL DEFAULT 'esp32_tcc',
    timestamp           DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    tensao_r            DECIMAL(6,2),
    tensao_s            DECIMAL(6,2),
    tensao_t            DECIMAL(6,2),
    corrente_r          DECIMAL(7,3),
    corrente_s          DECIMAL(7,3),
    corrente_t          DECIMAL(7,3),
    potencia_aparente   DECIMAL(9,2),   -- VA no instante do envio
    energia_kwh         DECIMAL(10,6),  -- delta de energia desde o último envio
    INDEX idx_timestamp (timestamp),
    INDEX idx_dispositivo_timestamp (dispositivo_id, timestamp)
) ENGINE=InnoDB;

-- ------------------------------------------------------------
-- Tabela de tarifas: permite atualizar o valor do kWh quando
-- a concessionária reajustar, mantendo o histórico de preços.
-- vigencia_fim = NULL significa "tarifa vigente atualmente"
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS tarifas (
    id                  INT AUTO_INCREMENT PRIMARY KEY,
    valor_kwh           DECIMAL(8,4) NOT NULL,  -- R$ por kWh
    vigencia_inicio     DATE NOT NULL,
    vigencia_fim        DATE NULL,
    observacao          VARCHAR(255)
) ENGINE=InnoDB;

-- -----------------------------------------------------------------------------------------
--  Exemplo: cadastre a tarifa atual da sua concessionária

INSERT INTO tarifas (valor_kwh, vigencia_inicio, vigencia_fim, observacao)
VALUES (0.95, '2026-01-01', NULL, 'Tarifa residencial - ajuste conforme sua fatura de luz');

-- ------------------------------------------------------------------------------------
--  COMANDO UPDATE UTILIZADO PARA SETAR OS VALORES do kWH em Reais da concessionária do MÊS.
UPDATE tarifas
SET valor_kwh = 1.48 
WHERE vigencia_fim IS NULL;

-- -------------------------------------------------------------------------------------
-- comando para verificar Kwh atual acumulado
SELECT  COALESCE(SUM(energia_kwh), 0) AS KWH_total
FROM leituras 
WHERE dispositivo_id = 'esp32_tcc'
   AND TIMESTAMP >= '2026-07-23'
   
-- -------------------------------------------------------------------------------------
-- COMANDO SELECT PARA CONSULTAR O VALOR DO kwh Reais DA TARIFA VIGENTE_FIM.
SELECT *
FROM tarifas
WHERE vigencia_fim IS NULL;

-- --------------------------------------------------------------------------------------
-- COMANDO PARA SABER SE EXISTE TABLE 
USE monitor_energia;
DESCRIBE ciclo_faturamentos;
SHOW CREATE TABLE ciclo_faturamentos;

-- --------------------------------------------------------------------------------------
-- COMANDO PARA SABER QUANTAS TABELAS EXITEM 
USE monitor_energia;
SHOW TABLES;

-- ----------------------------------------------------------------
-- CONTROLE DE FATURAS ENEL
-- inicio e fim dos ciclos de faturas do mês de Junho 
-- Leitura Atual 12/06/2026
-- proxima Leitura 13/07/2026
-- consumo em Kwh 203
-- Total a pagar 329.38 
-- -------------------------------------------------------------
-- inicio e fim dos ciclos de faturas do mês de Julho 
-- Leitura Atual 13/07/2026
-- proxima Leitura 13/08/2026
-- consumo em Kwh 196
-- Total a pagar 401,56
-- ------------------------------------------------------------
-- SUBSTITUIÇÃO DO MEDIDOR N° 795851
-- INICIO 23/07/2026
-- VALOR DA ULTIMA LEITURA DO MEDIDOR QUE SAIU 58594 
-- ------------------------------------------------------------
-- criação datas inico e fim das contas de quando a ENEL realiza as Leituras Ciclos mensais 
CREATE TABLE IF NOT EXISTS  ciclo_faturamentos (
  id                    INT AUTO_INCREMENT PRIMARY KEY, 
  data_leitura_atual    DATE NOT NULL,
  data_proxima_leitura  DATE NULL,
  leitura_kwh_anterior decimal(12,3) NULL,
  leitura_kwh_atual     DECIMAL(12,3) NULL,
  observacao  			varchar(255) 
  )ENGINE = InnoDB;

INSERT INTO ciclo_faturamentos (data_leitura_atual, data_proxima_leitura, observacao)
VALUES ('2026-07-23' , NULL , 'proxima_leitura com medidor trifasico novo')

-- ------------------------------------------------------------------------------
-- COMANDO UPDATE PARA SETAR VALORES DATA INICIO E FIM DE LEITURAS 
-- EM CASO DE MUDANÇA ERRADO UTILIZAR PARA RETORNAR DATA CORRETA
USE monitor_energia;
UPDATE ciclo_faturamentos
SET
    data_leitura_atual = '2026-07-23 12:00:00',
    data_proxima_leitura = '2026-08-13 12:00:00',
    leitura_kwh_atual = 0,
    leitura_kwh_anterior = 0,    
    observacao = 'data adcionado para proxima leitura da com medidor trifásico novo'
WHERE id = 1;

-- ---------------------------------------------------------------------------------
-- comando para verificar tabela com e quantos ciclos ja forma registrados.
USE monitor_energia;
SELECT
    id,
    data_leitura_atual,
    data_proxima_leitura,
    leitura_kwh_anterior,
    leitura_kwh_atual,
    observacao
FROM ciclo_faturamentos
ORDER BY id;

-- -------------------------------------------------------------------------- 
-- COMANDO PARA CONSULTAR OS REGISTROS E EVENTOS DA TABELA LEITURAS
-- DURANTE O PERIODO COM HORARIO BRASÍLIA
-- para consultar o periodo modificar as datas timestamp> 2026-08-07 12:00:00
USE monitor_energia;
SELECT
    id,
    dispositivo_id,
    TIMESTAMP AS horario_utc,
    CONVERT_TZ(timestamp, '+00:00', '-03:00') AS data_hora_Brazilia,
    tensao_r,
    tensao_s,
    tensao_t,
    corrente_r,
    corrente_s,
    corrente_t,
    potencia_aparente,
    energia_kwh
FROM leituras
WHERE timestamp >= '2026-08-16 03:00:00'
  AND timestamp <  '2026-08-17 03:00:00'
ORDER BY timestamp;

-- ----------------------------------------------------------------------------------
-- COMANDO PARA DELETAR id=4 ou qualquer id EM CASO DE MAIS INSERT ERRADO
USE monitor_energia;
DELETE FROM ciclo_faturamentos
WHERE id = 5;
