//https://github.com/rogerclarkmelbourne/Arduino_STM32/tree/master/STM32F1/libraries/USBComposite/examples
//important : release所有延迟必须要大于press 不然会产生斷触效果
#include <USBComposite.h>
#include <LinkedList.h>
USBHID HID;
HIDKeyboard Keyboard(HID);
//去抖参数
uint8_t diff = 8;  //去抖差值阈值
uint8_t capacity = 4;
uint8_t interrupt_cap = 27;  //中断阈值
//mrekk参数是 0.4 down0.1 up0.2设置
//0.4mm actuation=(256/40)*4 其他的相同方式计算 行程距离0-255
uint8_t actuation_num = 55;
uint8_t downstroke = 8;
uint8_t upstroke = 20;
uint8_t actuation_reset_point = 20;  //可以参考这个actuation_num - upstroke
struct {
  bool actuation;
  bool pressed;
  uint8_t current;
  uint8_t keycode;
  uint8_t last_actuation;

} keys[2];
class Event {
public:
  unsigned long time;
  uint8_t keycode;
  bool pressRelease;
};
LinkedList<Event *> keyEvent = LinkedList<Event *>();
int left_lst_value = 0;
int right_lst_value = 0;
int cur_left = 0;
int cur_right = 0;
void setup() {
  // Serial.begin(9600);
  pinMode(PA1, INPUT_ANALOG);
  pinMode(PA4, INPUT_ANALOG);
  HID.begin(HID_KEYBOARD);
  while (!USBComposite)
    ;

  delay(1500);
  //diff=rmjitter();
  left_lst_value = analogRead(PA1);
  right_lst_value = analogRead(PA4);
  keys[0].keycode = 'g';
  keys[1].keycode = 'h';
}


int rmjitter() {
  int max = 0;
  int min = 99999;
  int value = analogRead(PA3);

  for (int i = 0; i < 10000; i++) {
    value = analogRead(PA3);
    if (value > max)
      max = value = analogRead(PA4);
    if (value < min)

      min = analogRead(PA3);
  }
  return max - min;
}
void checkList() {
  Event *temp;
  unsigned long current = micros();  //因为这个命令会中断CPU所以不希望执行太多次
  for (uint8_t i = 0; i < keyEvent.size(); i++) {
    temp = keyEvent.pop();
    if (current < (temp->time)) {

      keyEvent.add(temp);
    } else {
      if (temp->pressRelease)
        pressKey(temp->keycode);
      else
        releaseKey(temp->keycode);
      delete temp;
    }
  }
}
uint8_t calcActuation(int input, int begin, int end, float segment_travel, int magnetic_turning_point) {
  //按照wooting的划分方式 max_gear把键盘分成40个档位 对应4mm行程就是最低0.1mm 3mm行程的话就是0.075mm 数值越高越靠近磁性越好吧 精度越高 maybe
  uint8_t max_gear = 255;
  int result = 0;
  int mid_gear = max_gear * segment_travel;
  if (input > begin) return 0;
  if (input < end) return max_gear;

  if (input > magnetic_turning_point) {
    //终止行程mid_gear
    //磁铁终止行程begin-magnetic_turning_point
    //行程input-magnetic_turning_point
    //代数：67-(2600-2500)*67/374=18
    result = mid_gear - ((input - magnetic_turning_point) * mid_gear) / (begin - magnetic_turning_point);
  } else {
    //终止行程mid_gear~max_gear
    //磁铁终止行程begin-magnetic_turning_point
    //行程input-magnetic_turning_point
    //(0~max_gear-mid_gear)+mid_gear
    //代数：100-(2000-1900)*33/600
    int range = max_gear - mid_gear;
    result = max_gear - ((input - end) * range / (magnetic_turning_point - end));
  }
  return result;
}
void calcAnalogLeft() {
  cur_left = 0;
  int temp_left = 0;
  for (uint8_t i = 0; i < capacity; i++) {
    temp_left = analogRead(PA1);
    int minus = temp_left - left_lst_value;
    if (minus > interrupt_cap || minus < -interrupt_cap) {
      //检测到不在抖动范围 即刻interrupt
      cur_left = temp_left;
      return;
    }
    cur_left += temp_left;
  }
  cur_left = cur_left / capacity;
}
void calcAnalogRight() {

  cur_right = 0;

  int temp_right = 0;
  for (uint8_t i = 0; i < capacity; i++) {
    temp_right = analogRead(PA4);
    int minus = temp_right - right_lst_value;
    if (minus > interrupt_cap || minus < -interrupt_cap) {
      cur_right = temp_right;
      return;
    }
    cur_right += temp_right;
  }

  cur_right = cur_right / capacity;
}

void addEvent(uint8_t code, bool pressRealease, uint8_t latency) {
  Event *e = new Event();
  e->time = micros() + latency;

  e->keycode = code;
  e->pressRelease = pressRealease;
  keyEvent.add(e);
}
void keyLogic(uint8_t n) {

  //是否actuation
  if (!keys[n].actuation) {
    if (keys[n].pressed)
      actuation_reset(n);
    if (keys[n].current >= actuation_num) {
      addEvent(n, true, 42000);
      keys[n].actuation = true;
      keys[n].last_actuation = keys[n].current;
    }
  } else {
    //更新last_actuation
    //1.没触发的时候 释放更新
    //2.触发的时候 下压更新
    if ((keys[n].last_actuation < keys[n].current && keys[n].pressed == true) || (keys[n].last_actuation > keys[n].current && keys[n].pressed == false)) {
      keys[n].last_actuation = keys[n].current;
    }
    //判断是否释放
    if (keys[n].last_actuation > keys[n].current) {
      if ((keys[n].last_actuation - keys[n].current > upstroke) && keys[n].pressed) {
        addEvent(n, false, 70000);
        return;
      }
    }
    //判断是否按下
    if ((keys[n].last_actuation + downstroke <= keys[n].current) && !keys[n].pressed) {
      addEvent(n, true, 10000);
      return;
    }
    //重置actuation
    if (keys[n].current < actuation_reset_point) {
      actuation_reset(n);
      return;
    }
  }
}
void actuation_reset(uint8_t n) {
  keys[n].actuation = false;
  keys[n].last_actuation = 0;
  addEvent(n, false, 70000);
}
void pressKey(uint8_t n) {
  Keyboard.press(keys[n].keycode);
  keys[n].pressed = true;
  debug(false);
}
void releaseKey(uint8_t n) {

  Keyboard.release(keys[n].keycode);
  keys[n].pressed = false;
  debug(false);
}
void debug(bool status) {
  //if(status)
  //Serial.println(status);
}
//运行一轮扫描逻辑要112us 扫描频率自己设置吧
int left_begin=2914;
int right_begin=2895;

void loop() {
  checkList();
  calcAnalogLeft();
  int temp = left_lst_value - cur_left;
  if (temp > diff || temp < -diff) {
    left_lst_value = cur_left;
    keys[0].current = calcActuation(cur_left, left_begin, 1950, 0.6667, 2500);
    keyLogic(0);
  }
if((cur_left>left_begin-diff-5)&&keys[0].pressed){
  //兜底用的 防止因为防抖导致数据逃逸断触
        actuation_reset(0);
  }

  calcAnalogRight();
  temp = right_lst_value - cur_right;
  if (temp > diff || temp < -diff) {
    right_lst_value = cur_right;
    keys[1].current = calcActuation(cur_right, right_begin, 1985, 0.81, 2400);
    keyLogic(1);
  }
    if((cur_right>right_begin-diff-5)&&keys[1].pressed){
        actuation_reset(1);
  }
  delayMicroseconds(500);

  // time=micros()-time;
  // Serial.print("Time: ");
  // Serial.println(time);


  // Serial.print(cur_left);
  // Serial.print(",");
  // Serial.println(cur_right);
}


/*
void loop() {
  calcAnalogLeft();
  calcAnalogRight();

  int temp=left_lst_value-cur_left;
  
if(temp>diff||temp<-diff){
  
  left_lst_value=cur_left;
 Serial.print(cur_left);

Serial.print(",");
Serial.print(calcActuation(cur_left,2863,1750,0.667,2450));
Serial.print(",");


Serial.print(cur_right);
Serial.print(",");
Serial.println(calcActuation(cur_right,2875,1600,0.56,2595));
delay(2);
return;
}*/
/*
temp=right_lst_value-cur_right;
if(temp>diff||temp<-diff){
  right_lst_value=cur_right;
Serial.print(calcActuation(cur_left,2863,1750,0.667,2450));
Serial.print(",");
Serial.println(calcActuation(cur_right,2875,1600,0.56,2595));
delay(2);
return;
}*/
