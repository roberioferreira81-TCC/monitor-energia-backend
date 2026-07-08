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

-- Exemplo: cadastre a tarifa atual da sua concessionária
INSERT INTO tarifas (valor_kwh, vigencia_inicio, vigencia_fim, observacao)
VALUES (0.95, '2026-01-01', NULL, 'Tarifa residencial - ajuste conforme sua fatura de luz');
