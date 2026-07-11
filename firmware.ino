//==================================================
// HX711 × 4 / XIAO ESP32S3
// RATEピンをVCCへ接続して80Hz動作
//
// 出力形式:
// raw_ch1,raw_ch2,raw_ch3,raw_ch4
//
// マイコン側では以下を行わない:
// ・ゼロ点キャリブレーション
// ・g換算
// ・メジアンフィルター
// ・移動平均
// ・スパイク除外
//==================================================

const uint8_t DT_PIN[4] = {
  D0,
  D2,
  D4,
  D6
};

const uint8_t CLK_PIN[4] = {
  D1,
  D3,
  D5,
  D7
};

const uint16_t HX711_SETTLING_MS = 150;
const uint32_t HX711_READY_TIMEOUT_US = 30000;

//--------------------------------------------------
// 4台すべてが変換完了するまで待つ
//--------------------------------------------------
bool waitAllHX711Ready(
  uint32_t timeoutUs = HX711_READY_TIMEOUT_US
)
{
  const uint32_t startUs = micros();

  while (
    digitalRead(DT_PIN[0]) == HIGH ||
    digitalRead(DT_PIN[1]) == HIGH ||
    digitalRead(DT_PIN[2]) == HIGH ||
    digitalRead(DT_PIN[3]) == HIGH
  )
  {
    if ((uint32_t)(micros() - startUs) >= timeoutUs)
    {
      return false;
    }

    yield();
  }

  return true;
}

//--------------------------------------------------
// HX711を1台読み取る
//--------------------------------------------------
int32_t readHX711Raw(uint8_t ch)
{
  uint32_t data = 0;

  // PD_SCKのHIGH期間が割り込みで長くなるのを防ぐ
  noInterrupts();

  for (uint8_t bit = 0; bit < 24; bit++)
  {
    digitalWrite(CLK_PIN[ch], HIGH);
    delayMicroseconds(1);

    data <<= 1;

    if (digitalRead(DT_PIN[ch]) == HIGH)
    {
      data |= 1UL;
    }

    digitalWrite(CLK_PIN[ch], LOW);
    delayMicroseconds(1);
  }

  // 25パルス目
  // 次回変換をChannel A / Gain 128に設定
  digitalWrite(CLK_PIN[ch], HIGH);
  delayMicroseconds(1);

  digitalWrite(CLK_PIN[ch], LOW);
  delayMicroseconds(1);

  interrupts();

  // HX711の24bit符号付き値を32bitへ符号拡張
  if (data & 0x800000UL)
  {
    data |= 0xFF000000UL;
  }

  return (int32_t)data;
}

//--------------------------------------------------
// 4台を順番に読み取る
//--------------------------------------------------
bool readAllHX711Raw(int32_t raw[4])
{
  if (!waitAllHX711Ready())
  {
    return false;
  }

  for (uint8_t ch = 0; ch < 4; ch++)
  {
    raw[ch] = readHX711Raw(ch);
  }

  return true;
}

//--------------------------------------------------
void setup()
{
  Serial.begin(115200);

  for (uint8_t ch = 0; ch < 4; ch++)
  {
    pinMode(DT_PIN[ch], INPUT);
    pinMode(CLK_PIN[ch], OUTPUT);

    digitalWrite(CLK_PIN[ch], LOW);
  }

  delay(HX711_SETTLING_MS);
}

//--------------------------------------------------
void loop()
{
  int32_t raw[4];

  if (!readAllHX711Raw(raw))
  {
    return;
  }

  Serial.print(raw[0]);
  Serial.print(',');

  Serial.print(raw[1]);
  Serial.print(',');

  Serial.print(raw[2]);
  Serial.print(',');

  Serial.println(raw[3]);
}