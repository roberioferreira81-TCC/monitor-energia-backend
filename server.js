require('dotenv').config();
const path = require('path');
const express = require('express');
const mysql = require('mysql2/promise');
const cors = require('cors');

const app = express();
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public'))); // serve o dashboard em /

// ------------------------------------------------------------
// Pool de conexões com o MySQL
// DB_SSL=true ativa SSL (necessário para bancos gerenciados como Aiven).
// DB_SSL_CA (opcional) = caminho para o certificado ca.pem baixado do Aiven.
// ------------------------------------------------------------
const poolConfig = {
  host: process.env.DB_HOST,
  port: process.env.DB_PORT || 3306,
  user: process.env.DB_USER,
  password: process.env.DB_PASSWORD,
  database: process.env.DB_NAME,
  Timezone: 'z',
  waitForConnections: true,
  connectionLimit: 10,
};

if (process.env.DB_SSL === 'true') {
  poolConfig.ssl = process.env.DB_SSL_CA
    ? { ca: require('fs').readFileSync(process.env.DB_SSL_CA) }
    : { rejectUnauthorized: false };
}

const pool = mysql.createPool(poolConfig);

// ------------------------------------------------------------
// POST /api/leituras
// Recebe uma leitura do ESP32 e grava no banco.
// Espera JSON:
// {
//   "dispositivo_id": "esp32_tcc",
//   "tensao_r": 220.1, "tensao_s": 219.8, "tensao_t": 221.0,
//   "corrente_r": 1.2, "corrente_s": 0.8, "corrente_t": 1.5,
//   "potencia_aparente": 550.3,
//   "energia_kwh": 0.003056   <- delta desde o último envio
// }
// ------------------------------------------------------------
app.post('/api/leituras', async (req, res) => {
  try {
    const {
      dispositivo_id = 'esp32_tcc',
      tensao_r, tensao_s, tensao_t,
      corrente_r, corrente_s, corrente_t,
      potencia_aparente,
      potencia_ativa,
      energia_kwh,
    } = req.body;

    if (energia_kwh === undefined || potencia_aparente === undefined) {
      return res.status(400).json({ erro: 'Campos obrigatórios ausentes.' });
    }

    await pool.query(
      `INSERT INTO leituras
        (dispositivo_id, tensao_r, tensao_s, tensao_t,
         corrente_r, corrente_s, corrente_t,
         potencia_aparente, energia_kwh)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      [dispositivo_id, tensao_r, tensao_s, tensao_t,
       corrente_r, corrente_s, corrente_t,
       potencia_aparente, energia_kwh]
    );

    res.status(201).json({ status: 'ok' });
  } catch (err) {
    console.error('Erro ao inserir leitura:', err);
    res.status(500).json({ erro: 'Erro interno ao gravar leitura.' });
  }
});

// ------------------------------------------------------------
// GET /api/tarifa/vigente
// Retorna a tarifa (R$/kWh) atualmente em vigor.
// ------------------------------------------------------------
async function buscarTarifaVigente(dataRef = new Date()) {
  const dataStr = dataRef.toISOString().slice(0, 10);
  const [rows] = await pool.query(
    `SELECT valor_kwh FROM tarifas
     WHERE vigencia_inicio <= ?
       AND (vigencia_fim IS NULL OR vigencia_fim >= ?)
     ORDER BY vigencia_inicio DESC
     LIMIT 1`,
    [dataStr, dataStr]
  );
  return rows.length ? Number(rows[0].valor_kwh) : null;
}

app.get('/api/tarifa/vigente', async (req, res) => {
  try {
    const tarifa = await buscarTarifaVigente();
    if (tarifa === null) {
      return res.status(404).json({ erro: 'Nenhuma tarifa cadastrada para a data atual.' });
    }
    res.json({ valor_kwh: tarifa });
  } catch (err) {
    console.error(err);
    res.status(500).json({ erro: 'Erro ao buscar tarifa.' });
  }
});

// ------------------------------------------------------------
// GET /api/consumo/mensal?ano=2026&mes=7&dispositivo_id=esp32_tcc
// Soma a energia (kWh) do mês e calcula o valor em R$.
// Se ano/mes não forem informados, usa o mês atual.
// ------------------------------------------------------------
app.get('/api/consumo/mensal', async (req, res) => {
  try {
    const hoje = new Date();
    const ano = parseInt(req.query.ano) || hoje.getFullYear();
    const mes = parseInt(req.query.mes) || (hoje.getMonth() + 1);
    const dispositivo_id = req.query.dispositivo_id || 'esp32_tcc';

    const [rows] = await pool.query(
      `SELECT COALESCE(SUM(energia_kwh), 0) AS kwh_total
       FROM leituras
       WHERE dispositivo_id = ?
         AND YEAR(timestamp) = ?
         AND MONTH(timestamp) = ?`,
      [dispositivo_id, ano, mes]
    );

    const kwhTotal = Number(rows[0].kwh_total);
    const tarifa = await buscarTarifaVigente(new Date(ano, mes - 1, 1));

    if (tarifa === null) {
      return res.status(404).json({ erro: 'Nenhuma tarifa cadastrada para este período.' });
    }

    const valorReais = kwhTotal * tarifa;

    res.json({
      ano,
      mes,
      dispositivo_id,
      kwh_total: Number(kwhTotal.toFixed(4)),
      tarifa_kwh: tarifa,
      valor_reais: Number(valorReais.toFixed(2)),
    });
  } catch (err) {
    console.error(err);
    res.status(500).json({ erro: 'Erro ao calcular consumo mensal.' });
  }
});
//------------------------------------------------------
//GET /api/consumo/ciclos_faturamentos?ano=2026&mes=7&dispositivo_id=esp32_tcc
// Soma a energia (kWh) do mês e calcula o valor em R$.
//------------------------------------------------------
app.get('/api/consumo/ciclo', async (req, res) => {
  try {
    const dispositivo_id = req.query.dispositivo_id || 'esp32_tcc';

    // PASSO 1 — Buscar a data de início do ciclo em aberto
    const [cicloRows] = await pool.query(
      `SELECT data_leitura_atual
       FROM ciclo_faturamentos
       WHERE data_proxima_leitura is null;`   // <-- preencha aqui (você já escreveu isso no DBeaver!)
    );

    if (cicloRows.length === 0) {
      return res.status(404).json({ erro: 'Nenhum ciclo em aberto cadastrado.' });
    }

    const dataInicio = cicloRows[0].data_leitura_atual;

    // PASSO 2 — Somar energia_kwh a partir dessa data
    const [leiturasRows] = await pool.query(
      `SELECT COALESCE(SUM(energia_kwh), 0) AS kwh_total
       FROM leituras
       WHERE dispositivo_id = ?
         AND timestamp >= ?`,
      [dispositivo_id, dataInicio]
    );

    const kwhTotal = Number(leiturasRows[0].kwh_total);

    // PASSO 3 — Buscar a tarifa vigente e calcular o valor
    const tarifa = await buscarTarifaVigente();
    const valorReais = kwhTotal * tarifa;

    res.json({
      dispositivo_id,
      data_inicio_ciclo: dataInicio,
      kwh_total: Number(kwhTotal.toFixed(4)),
      tarifa_kwh: tarifa,
      valor_reais: Number(valorReais.toFixed(2)),
    });
  } catch (err) {
    console.error(err);
    res.status(500).json({ erro: 'Erro ao calcular consumo do ciclo.' });
  }
});

// ------------------------------------------------------------
// GET /api/leituras/ultimas?limite=20
// Últimas leituras registradas (útil para dashboard/depuração)
// ------------------------------------------------------------
app.get('/api/leituras/ultimas', async (req, res) => {
  try {
    const limite = Math.min(parseInt(req.query.limite) || 20, 500);
    const [rows] = await pool.query(
      `SELECT * FROM leituras ORDER BY timestamp DESC LIMIT ?`,
      [limite]
    );
    res.json(rows);
  } catch (err) {
    console.error(err);
    res.status(500).json({ erro: 'Erro ao buscar leituras.' });
  }
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`Servidor rodando na porta ${PORT}`);
});
