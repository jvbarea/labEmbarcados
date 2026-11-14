#include "DoorActuator.h"

const char *doorStateName(DoorState s) {
  switch (s) {
    case DoorState::CLOSED:      return "closed";
    case DoorState::OPENING:     return "opening";
    case DoorState::OPEN:        return "open";
    case DoorState::CLOSING:     return "closing";
    case DoorState::FAULT:       return "fault";
    case DoorState::CALIBRATING: return "calibrating";
    default:                     return "unknown";
  }
}

// Linear entre os dois pontos medidos: us0 em 0 graus, us90 em 90. Acima de 90
// extrapola na mesma reta, sempre limitado a faixa absoluta de seguranca.
int DoorActuator::degToUs(int deg) const {
  deg = constrain(deg, 0, 180);
  long us = (long)us0_ + lroundf((float)(us90_ - us0_) * (float)deg / 90.0f);
  return (int)constrain(us, (long)SERVO_US_MIN, (long)SERVO_US_MAX);
}

int DoorActuator::usToDeg(int us) const {
  int span = us90_ - us0_;
  if (span == 0) return 0;  // setServoCalibration impede, mas dividir por zero e pior
  long deg = lroundf((float)(us - us0_) * 90.0f / (float)span);
  return (int)constrain(deg, 0L, 180L);
}

bool DoorActuator::setServoCalibration(int us0, int us90) {
  const bool dentro = us0  >= SERVO_US_MIN && us0  <= SERVO_US_MAX &&
                      us90 >= SERVO_US_MIN && us90 <= SERVO_US_MAX;
  if (!dentro) return false;
  if (abs(us90 - us0) < SERVO_CAL_MIN_SPAN_US) return false;

  us0_  = us0;
  us90_ = us90;
  return true;
}

void DoorActuator::begin(int closedDeg, int openDeg, int us0, int us90) {
  // A ESP32Servo exige timer alocado antes do attach. Faltava no original
  // (REVISAO.md, item 3).
  ESP32PWM::allocateTimer(0);

#if USE_LIMIT_SWITCHES
  pinMode(PIN_LIMIT_OPEN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_CLOSED, INPUT_PULLUP);
#endif

  setPositions(closedDeg, openDeg);

  // Calibracao invalida na NVS (corrompida, ou gravada por outra versao): fica
  // com o padrao e avisa, em vez de escrever pulsos sem sentido no servo.
  if (!setServoCalibration(us0, us90)) {
    Serial.printf("[servo] calibracao invalida (%d, %d us) - usando o padrao %d, %d\n",
                  us0, us90, us0_, us90_);
  }

  // Homing: leva a porta ate a posicao de fechada, para partir de um estado
  // conhecido mesmo que alguem a tenha movido com a mao.
  currentUs_ = degToUs(closedDeg_);
  startMovement(DoorState::CLOSING, currentUs_);
}

void DoorActuator::setPositions(int closedDeg, int openDeg) {
  closedDeg_ = constrain(closedDeg, 0, 180);
  openDeg_   = constrain(openDeg, 0, 180);
}

bool DoorActuator::limitOpenActive() const {
#if USE_LIMIT_SWITCHES
  return digitalRead(PIN_LIMIT_OPEN) == LIMIT_SWITCH_ACTIVE_LEVEL;
#else
  return false;
#endif
}

bool DoorActuator::limitClosedActive() const {
#if USE_LIMIT_SWITCHES
  return digitalRead(PIN_LIMIT_CLOSED) == LIMIT_SWITCH_ACTIVE_LEVEL;
#else
  return false;
#endif
}

bool DoorActuator::fcClosed() const {
#if USE_LIMIT_SWITCHES
  return limitClosedActive();
#else
  return state_ == DoorState::CLOSED;
#endif
}

bool DoorActuator::fcOpen() const {
#if USE_LIMIT_SWITCHES
  return limitOpenActive();
#else
  return state_ == DoorState::OPEN;
#endif
}

// Pulso em microssegundos vira "passos" do timer de 16 bits: 20000 us = 65536.
// Sempre limitado a faixa absoluta, qualquer que seja quem chamou.
void DoorActuator::writeUs(int us) {
  us = constrain(us, SERVO_US_MIN, SERVO_US_MAX);
  const uint32_t duty = (uint32_t)(((uint64_t)us * (1ULL << SERVO_PWM_BITS) +
                                    SERVO_PWM_PERIOD_US / 2) / SERVO_PWM_PERIOD_US);
  pwm_.write(duty);
}

void DoorActuator::energize() {
  if (!pwm_.attached()) pwm_.attachPin(PIN_SERVO, SERVO_PWM_FREQ_HZ, SERVO_PWM_BITS);
}

void DoorActuator::deenergize() {
  if (pwm_.attached()) pwm_.detachPin(PIN_SERVO);  // RNF04: servo desenergizado parado
}

void DoorActuator::startMovement(DoorState movingState, int targetUs) {
  targetUs_ = constrain(targetUs, SERVO_US_MIN, SERVO_US_MAX);
  state_ = movingState;
  moveStartMs_ = millis();
  lastStepMs_ = millis();
  settling_ = false;
  kicking_ = false;
  kickHoldStartMs_ = 0;
  energize();
  writeUs(currentUs_);
}

void DoorActuator::finishMovement(DoorState finalState) {
#if DOOR_HOLD_OPEN
  // Aberta: o servo segue energizado, segurando a tampa. O PWM do LEDC continua
  // sozinho no ultimo pulso, entao nao ha nada a escrever. Fechada e em falha ele
  // solta: fechada a tampa se apoia sozinha, e em falha nao ha por que forcar o motor.
  if (finalState != DoorState::OPEN) deenergize();
#else
  deenergize();  // RNF04: servo desenergizado ao fim do movimento
#endif
  settling_ = false;
  kicking_ = false;
  state_ = finalState;
}

void DoorActuator::requestOpen() {
  if (state_ == DoorState::OPEN || state_ == DoorState::OPENING) return;
  if (state_ == DoorState::CALIBRATING) return;

  // So sai do repouso fechado ganha partida suave. Se a porta ja estava andando
  // (fechando e alguem pede para abrir), o atrito estatico ja foi vencido e uma
  // pausa no meio do caminho so atrasaria.
  const bool doRepouso = (state_ == DoorState::CLOSED);
  startMovement(DoorState::OPENING, degToUs(openDeg_));
  if (doRepouso) planejaPartida(targetUs_);
}

void DoorActuator::requestClose() {
  if (state_ == DoorState::CLOSED || state_ == DoorState::CLOSING) return;
  if (state_ == DoorState::CALIBRATING) return;
  startMovement(DoorState::CLOSING, degToUs(closedDeg_));
}

void DoorActuator::reset() {
  startMovement(DoorState::CLOSING, degToUs(closedDeg_));
}

// ----------------------------------------------------------- calibracao

void DoorActuator::enterCalibration() {
  state_ = DoorState::CALIBRATING;
  targetUs_ = currentUs_;
  settling_ = false;
  kicking_ = false;
  kickHoldStartMs_ = 0;
  lastStepMs_ = millis();
  energize();
  writeUs(currentUs_);
}

void DoorActuator::exitCalibration() {
  if (state_ != DoorState::CALIBRATING) return;
  startMovement(DoorState::CLOSING, degToUs(closedDeg_));
}

bool DoorActuator::jogToDeg(int deg) {
  if (state_ != DoorState::CALIBRATING) return false;

  const bool parado = (currentUs_ == targetUs_) && !kicking_;
  kicking_ = false;
  kickHoldStartMs_ = 0;

  const int alvo = degToUs(deg);
  targetUs_ = alvo;

  // Saindo do repouso na posicao fechada, em direcao a aberta: mesma partida
  // suave do 'open'. E ali que o atrito estatico aparece, e foi onde o teste
  // 'pos 0' seguido de 'pos 90' falhava.
  const int fechadaUs = degToUs(closedDeg_);
  const int abertaUs  = degToUs(openDeg_);
  const bool naFechada = abs(currentUs_ - fechadaUs) <= SERVO_STEP_US_CAL;
  const bool paraAberta = ((alvo - currentUs_) > 0) == ((abertaUs - fechadaUs) > 0);

  if (parado && naFechada && paraAberta) return planejaPartida(alvo);
  return false;
}

// jogToUs e nudge sao o operador no controle: cancelam qualquer partida em curso.
void DoorActuator::jogToUs(int us) {
  if (state_ != DoorState::CALIBRATING) return;
  kicking_ = false;
  kickHoldStartMs_ = 0;
  targetUs_ = constrain(us, SERVO_US_MIN, SERVO_US_MAX);
}

// Parte do ALVO e nao da posicao atual: com a rampa, tres toques seguidos em '+'
// chegam antes de o eixo terminar o primeiro passo, e partir da posicao atual
// perderia dois deles.
void DoorActuator::nudge(int deltaUs) {
  if (state_ != DoorState::CALIBRATING) return;
  kicking_ = false;
  kickHoldStartMs_ = 0;
  targetUs_ = constrain(targetUs_ + deltaUs, SERVO_US_MIN, SERVO_US_MAX);
}

// -------------------------------------------------------- partida suave

// Divide o movimento em dois: um primeiro trecho curto e lento, e o resto.
// Chamar logo depois de definir targetUs_ com o destino verdadeiro.
bool DoorActuator::planejaPartida(int alvoUs) {
  if (DOOR_KICK_DEG <= 0) return false;

  const int total  = alvoUs - currentUs_;
  const int trecho = abs(us90_ - us0_) * DOOR_KICK_DEG / 90;

  // Se o curso inteiro mal passa do primeiro trecho, nao ha o que dividir.
  if (trecho <= 0 || abs(total) <= trecho + SERVO_STEP_US_OPEN) return false;

  finalTargetUs_ = alvoUs;
  targetUs_ = currentUs_ + (total > 0 ? trecho : -trecho);
  kicking_ = true;
  kickHoldStartMs_ = 0;
  return true;
}

// Devolve true enquanto a partida ocupa este ciclo (andando o primeiro trecho ou
// segurando a pausa); o chamador nao deve mover o eixo nesse caso. Ao fim da
// pausa troca o alvo pelo destino verdadeiro e devolve false.
bool DoorActuator::andaPartida() {
  if (!kicking_) return false;

  if (!stepTowardTarget()) return true;  // ainda indo ate o fim do primeiro trecho

  if (kickHoldStartMs_ == 0) kickHoldStartMs_ = millis();
  if (millis() - kickHoldStartMs_ < DOOR_KICK_HOLD_MS) return true;  // segurando

  kicking_ = false;
  kickHoldStartMs_ = 0;
  targetUs_ = finalTargetUs_;
  return false;
}

// --------------------------------------------------------------- update

bool DoorActuator::stepTowardTarget() {
  if (currentUs_ == targetUs_) return true;
  if (millis() - lastStepMs_ < SERVO_STEP_MS) return false;
  lastStepMs_ = millis();

  // Passo por situacao: a partida anda devagar, depois abre rapido, fecha mais
  // devagar, e na calibracao anda no passo suave original.
  int passo = SERVO_STEP_US_CAL;
  if (kicking_)                          passo = SERVO_STEP_US_KICK;
  else if (state_ == DoorState::OPENING) passo = SERVO_STEP_US_OPEN;
  else if (state_ == DoorState::CLOSING) passo = SERVO_STEP_US_CLOSE;

  int falta = targetUs_ - currentUs_;
  if (abs(falta) <= passo) currentUs_ = targetUs_;
  else                     currentUs_ += (falta > 0) ? passo : -passo;

  writeUs(currentUs_);
  return currentUs_ == targetUs_;
}

void DoorActuator::update() {
  // Em calibracao o servo fica energizado e segue o alvo, sem timeout nem
  // transicao automatica: quem manda e o operador.
  if (state_ == DoorState::CALIBRATING) {
    if (!andaPartida()) stepTowardTarget();
    return;
  }

  if (state_ != DoorState::OPENING && state_ != DoorState::CLOSING) return;

  const bool opening = (state_ == DoorState::OPENING);

  // Guarda de tempo: vale em qualquer fase do movimento.
  if (millis() - moveStartMs_ > DOOR_MOVE_TIMEOUT_MS) {
    finishMovement(DoorState::FAULT);
    return;
  }

  // A chave de fim de curso, quando existe, tem prioridade sobre a rampa.
  if ((opening && limitOpenActive()) || (!opening && limitClosedActive())) {
    finishMovement(opening ? DoorState::OPEN : DoorState::CLOSED);
    return;
  }

  // Partida suave da abertura, quando houver: ocupa o ciclo ate a pausa acabar.
  if (andaPartida()) return;

  if (settling_) {
    if (millis() - settleStartMs_ >= SERVO_SETTLE_MS) {
      finishMovement(opening ? DoorState::OPEN : DoorState::CLOSED);
    }
    return;
  }

  if (stepTowardTarget()) {
    settling_ = true;
    settleStartMs_ = millis();
  }
}
