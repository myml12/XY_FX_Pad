//==============================
// HX711設定 (RATEピン → VCC で 80Hz)
//==============================
const uint8_t DT_PIN[4] = {D0, D2, D4, D6};
const uint8_t CLK_PIN[4] = {D1, D3, D5, D7};

const uint16_t HX711_SAMPLE_RATE_HZ = 80;
const uint16_t HX711_SETTLING_MS = 150;  // 80Hz時の初回安定時間 (データシート: 約100ms)
const uint32_t SAMPLE_PERIOD_US = 1000000UL / HX711_SAMPLE_RATE_HZ;  // 12500us

// ゼロ点キャリブレーション設定
const uint16_t CALIB_WARMUP_SAMPLES = 80;   // 起動直後の不安定分を捨てる (約1秒)
const uint16_t CALIB_SAMPLES = 120;         // メジアン用に取得する数 (約1.5秒)

// SC616C 500g
#define OUT_VOL 0.0007f
#define LOAD 500.0f

float offset[4];

//---------------------------------
// HX711読み取り
//---------------------------------
long readHX711(uint8_t ch) {
  long data = 0;

  for (int i = 0; i < 24; i++) {
    digitalWrite(CLK_PIN[ch], HIGH);
    delayMicroseconds(1);

    data <<= 1;

    if (digitalRead(DT_PIN[ch]))
      data++;

    digitalWrite(CLK_PIN[ch], LOW);
    delayMicroseconds(1);
  }

  // Gain = 128
  digitalWrite(CLK_PIN[ch], HIGH);
  delayMicroseconds(1);
  digitalWrite(CLK_PIN[ch], LOW);

  return data ^ 0x800000;
}

//---------------------------------
float rawToGram(long raw) {
  const float AVDD = 4.2987f;
  const float ADC1bit = AVDD / 16777216.0f;
  const float SCALE = OUT_VOL * AVDD / LOAD * 128.0f;

  return raw * ADC1bit / SCALE;
}

//---------------------------------
// 配列のメジアンを求める (挿入ソート後に中央値)
//---------------------------------
float median(float *v, int n) {
  for (int i = 1; i < n; i++) {
    float key = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > key) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = key;
  }

  if (n & 1)
    return v[n / 2];
  return (v[n / 2 - 1] + v[n / 2]) * 0.5f;
}

//---------------------------------
// 全CHのDTがLOWになるまで待つ (変換完了)
//---------------------------------
void waitHX711Ready() {
  while (digitalRead(DT_PIN[0]) || digitalRead(DT_PIN[1]) ||
         digitalRead(DT_PIN[2]) || digitalRead(DT_PIN[3]))
    ;
}

//---------------------------------
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(DT_PIN[i], INPUT);
    pinMode(CLK_PIN[i], OUTPUT);
    digitalWrite(CLK_PIN[i], LOW);
  }

  delay(HX711_SETTLING_MS);

  //-----------------------------
  // ゼロ点キャリブレーション
  //  1) 起動直後の不安定分を読み捨てる
  //  2) 約1.5秒取得し、CHごとにメジアンで0点を決める
  //-----------------------------

  // 1) ウォームアップ (読み捨て)
  for (int n = 0; n < CALIB_WARMUP_SAMPLES; n++) {
    waitHX711Ready();
    for (int i = 0; i < 4; i++)
      readHX711(i);
  }

  // 2) サンプル収集
  static float buf[4][CALIB_SAMPLES];

  for (int n = 0; n < CALIB_SAMPLES; n++) {
    waitHX711Ready();
    for (int i = 0; i < 4; i++)
      buf[i][n] = rawToGram(readHX711(i));
  }

  // 3) CHごとのメジアンをゼロ点に
  for (int i = 0; i < 4; i++)
    offset[i] = median(buf[i], CALIB_SAMPLES);

  // ここでは何も出力しない
}

//---------------------------------
void loop() {
  static uint32_t next_sample_us = 0;

  // 80Hz周期に合わせて次サンプル時刻まで待機
  if (next_sample_us != 0) {
    while ((int32_t)(micros() - next_sample_us) < 0)
      ;
  }

  waitHX711Ready();

  float g[4];

  for (int i = 0; i < 4; i++)
    g[i] = rawToGram(readHX711(i)) - offset[i];

  next_sample_us = micros() + SAMPLE_PERIOD_US;

  // CSVのみ出力
  Serial.print(g[0], 3);
  Serial.print(",");
  Serial.print(g[1], 3);
  Serial.print(",");
  Serial.print(g[2], 3);
  Serial.print(",");
  Serial.println(g[3], 3);
}