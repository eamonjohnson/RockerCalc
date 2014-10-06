#include <pebble.h>

//The width of the screen is 144 pixels and the height is 168 pixels.
//The height of the status bar is 16 pixels.

// TODO: Add screen to configure sensitivity, threshold, and tracking speed (size of slow buffer).
//       Screen will have a circle indicating threshold and a line tracking the difference vector.  
  
#define BUF_SIZE_BUTTON 5
#define BUF_SIZE_NUMBER 64
#define ARRAY_SIZE_BUTTONS 32
#define BUF_SIZE_DEBUG 256
#define SCREEN_WIDTH 144
#define SCREEN_HEIGHT 168
#define STATUSBAR_HEIGHT 16
  
#define SIZE_BUTTON_X 22
#define SIZE_BUTTON_Y 22  
#define POSITION_BUTTONS_X 10
#define POSITION_BUTTONS_Y 60
#define POSITION_OP_X 5
#define POSITION_OP_Y 16
#define POSITION_NUM_X 37
#define POSITION_NUM_Y 16
#define SIZE_OP_X 22
#define SIZE_OP_Y 32
#define SIZE_NUM_X 100
#define SIZE_NUM_Y 32 
  
#define BUTTON_TYPE_NUMBER 0
#define BUTTON_TYPE_FUNCTION 1
#define BUTTON_FUNCTION_EQU 0 // equals
#define BUTTON_FUNCTION_ADD 1 // add
#define BUTTON_FUNCTION_SUB 2 // subtract
#define BUTTON_FUNCTION_MUL 3 // multiply
#define BUTTON_FUNCTION_DIV 4 // divide
#define BUTTON_FUNCTION_BAC 5 // backspace
#define BUTTON_FUNCTION_DOT 6 // dot
#define BUTTON_FUNCTION_CLE 7 // clear  

#define CALC_MODE_NEWOP 0  // mode where pressing a number will start a new operation
#define CALC_MODE_MIDOP 1  // mode where pressing a number will replace the current screen, but not start a new operation 
#define CALC_MODE_INPUT 2  // mode where pressing a number will add to the current number  
#define CALC_NUM_INITIAL  "0" // initial string for number display
#define CALC_OP_INITIAL  "" // initial string for operator display
  
bool LOGGING = false; // turning off logging seems to increase stability
bool DEBUG = false;

// global memory for things linked to GUI b/c it has a hard time w the heap
// these resources are allocated once by ID and not released until cleanup
#define GLOBAL_WINDOW_COUNT 4
#define GLOBAL_TEXTLAYER_COUNT 32
#define GLOBAL_BUFFER_COUNT 32
#define GLOBAL_BUFFER_SIZE 16  
#define GLOBAL_INVLAYER_COUNT 4  
Window* _windows[GLOBAL_WINDOW_COUNT];
int _windowsn = 0;
TextLayer* _textlayers[GLOBAL_TEXTLAYER_COUNT];
int _textlayersn = 0;
char _buffers[GLOBAL_BUFFER_COUNT][GLOBAL_BUFFER_SIZE];
int _buffersn = 0;
InverterLayer* _invlayers[GLOBAL_INVLAYER_COUNT];
int _invlayersn = 0;

void init_global_resources(){
  memset(_windows, 0, GLOBAL_WINDOW_COUNT);
  memset(_textlayers, 0, GLOBAL_TEXTLAYER_COUNT);
  memset(_invlayers, 0, GLOBAL_INVLAYER_COUNT);  
}

// Trying to see if callback is barfing the destructor.
// Not sure if this helps, or if the perceived increase in
// stability was due entirely to turning off logging.
bool shutdown = false;

// debug display
TextLayer* tl_debug;
char tl_debug_buf[256];
char buf_debug[BUF_SIZE_DEBUG]; // used as temp space for writing to logger
char tl_debug_buf2[256];

int getWindow(){
  _windows[_windowsn] = window_create();
  if(_windows[_windowsn] == NULL){
    snprintf(buf_debug, BUF_SIZE_DEBUG, "Unable to create _window %d", _windowsn); APP_LOG(APP_LOG_LEVEL_ERROR, buf_debug);    
    return -1;
  }
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Got _window %d", _windowsn); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);  
  return _windowsn++;
}

int getInvlayer(GRect rect){
  _invlayers[_invlayersn] = inverter_layer_create(rect);
  if(_invlayers[_invlayersn] == NULL){
    snprintf(buf_debug, BUF_SIZE_DEBUG, "Unable to create _invlayer %d", _invlayersn); APP_LOG(APP_LOG_LEVEL_ERROR, buf_debug);    
    return -1;
  }
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Got _invlayer %d", _invlayersn); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);  
  return _invlayersn++;
}

int getTextlayer(GRect rect){
  _textlayers[_textlayersn] = text_layer_create(rect);
  if(_textlayers[_textlayersn] == NULL){
    snprintf(buf_debug, BUF_SIZE_DEBUG, "Unable to create _textlayer %d", _textlayersn); APP_LOG(APP_LOG_LEVEL_ERROR, buf_debug);    
    return -1;
  }
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Got _textlayer %d", _textlayersn); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);  
  return _textlayersn++;
}

int getBuffer(){
  memset(_buffers[_buffersn], 0, GLOBAL_BUFFER_SIZE);
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Got _buffer %d", _buffersn); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);  
  return _buffersn++;
}

typedef struct{
  int x;
  int y;
  int z;
} rc_vector3;

typedef struct{
  int x;
  int y;
} rc_vector2;

#define SMOOTHER_BUF_SIZE_MAX 128
typedef struct{
  int n;
  int p;
  int buf[SMOOTHER_BUF_SIZE_MAX];
  int sum;  
  int size;
} rc_smoother;


typedef struct{
  rc_smoother sx;
  rc_smoother sy;
  rc_smoother sz;
} rc_smoothvector3;

void rc_setup_smoothvector3(rc_smoothvector3* s, int size){
  if(size > SMOOTHER_BUF_SIZE_MAX) size = SMOOTHER_BUF_SIZE_MAX;
  memset(s, 0, sizeof(rc_smoothvector3));
  s->sx.size = size;
  s->sy.size = size;
  s->sz.size = size;
}

void rc_update_smoother(rc_smoother* s, int sample){
  if(s->n < s->size) s->n++;
  s->sum -= s->buf[s->p];  
  s->buf[s->p] = sample;
  s->sum += sample;
  s->p = (s->p+1) % s->size;
}

int rc_get_smoother_value(rc_smoother* s){
  if(s->n == 0) return 0;
  return s->sum / s->n;
}

void rc_update_smoothvector3(rc_smoothvector3* sv, AccelData* ad, uint32_t num_samples){
  for(uint32_t i = 0; i < num_samples; i++){
    rc_update_smoother(&(sv->sx), ad[i].x);
    rc_update_smoother(&(sv->sy), ad[i].y);
    rc_update_smoother(&(sv->sz), ad[i].z);
  }
}
rc_smoothvector3 vslow;
rc_smoothvector3 vfast;
//rc_vector3 vbase = {.x=0, .y=0, .z=0}; 
//rc_vector3 vcurr = {.x=0, .y=0, .z=0};
rc_vector3 vdiff = {.x=0, .y=0, .z=0};
//rc_vector2 vtilt = {.x=0, .y=0};
int tilt=0;
uint64_t tilt_time=0;
uint16_t scan_time=0;
/*  fast inverse sqrt code from Quake 3, won't compile on cloudpebble
float Q_rsqrt( float number )
{
	long i;
	float x2, y;
	const float threehalfs = 1.5F; 
	x2 = number * 0.5F;
	y  = number;
	i  = * ( long * ) &y;                       // evil floating point bit level hacking
	i  = 0x5f3759df - ( i >> 1 );               // what the fuck?
	y  = * ( float * ) &i;
	y  = y * ( threehalfs - ( x2 * y * y ) );   // 1st iteration
  //      y  = y * ( threehalfs - ( x2 * y * y ) );   // 2nd iteration, this can be removed
	return y;
}
*/

typedef struct{
  int top;
  int left;
  //char label[BUF_SIZE_BUTTON];
  //TextLayer* tl;
  int tlid;  // ID of the TextLayer
  int bufid; // ID of the buffer for the label
  int type;
  int value;
} rc_button;
  
typedef struct{
  rc_button* buttons[ARRAY_SIZE_BUTTONS];
  int count;
  int curx; // layout cursor x
  int cury; // layout cursor y
  int curb; // current button
} rc_buttonset;

typedef struct{
  int invid;
  int x;
  int y;
} rc_cursor;

typedef struct{
  rc_buttonset* buttonset;
  int tlid_op; // id of textlayer for currect operator display
  int bufid_op; // id of buffer for operator display
  int tlid_num; // id of textlayer for number display
  int bufid_num; // id of buffer for number display
  int winid; // window id
  rc_cursor cursor;
  float tmpval; // value in temporary memory
  int tmpop;    // operator ready
  int mode;     // state of the calculator
} rc_calculator;

// global pointer to calculator
rc_calculator* calc = NULL;

// Threshold of 75 seems good to be able to nudge the watch to move the cursor
// Sampling is set to 50Hz, 4 samples per batch.
// Smoothing is 4 samples for the fast vector, 64 samples for the slow vector.
#define THRESH_TILT_X 75 // milliG to trigger tilt
#define THRESH_TILT_Y 75 // milliG to trigger tilt
#define THRESH_TIME 0 // millis between cursor moves  

void update_cursor(){
  int x = calc->cursor.x;
  int y = calc->cursor.y;
  bool update = false; // true if cursor needs an update
  //if(tilt==7 || tilt==8 || tilt==9){ // plus y
  if(tilt > 6){ // plus y
    if(y > (POSITION_BUTTONS_Y)){ // the +1 is just shit
      update = true;
      y -= SIZE_BUTTON_Y;
    }
  }
  //if(tilt==1 || tilt==2 || tilt==3){ // minus y
  if(tilt < 4){ // minus y
    if(y < (POSITION_BUTTONS_Y + SIZE_BUTTON_Y * 2)){ // there are three rows
      update = true;
      y += SIZE_BUTTON_Y;
    }    
  }
  if(tilt==1 || tilt==4 || tilt==7){ // minus x
    if(x > (POSITION_BUTTONS_X)){
      update = true;
      x -= SIZE_BUTTON_X;
    }
  }
  if(tilt==3 || tilt==6 || tilt==9){ // plus x
    if(x < (POSITION_BUTTONS_X + SIZE_BUTTON_X * 5)){ // there are six columns
      update = true;
      x += SIZE_BUTTON_X;
    }
  }
  if(update){
    inverter_layer_destroy(_invlayers[calc->cursor.invid]);
    _invlayers[calc->cursor.invid] = inverter_layer_create(GRect(x, y, SIZE_BUTTON_X, SIZE_BUTTON_Y));
    layer_add_child(window_get_root_layer(_windows[calc->winid]), inverter_layer_get_layer(_invlayers[calc->cursor.invid]));
    calc->cursor.x = x;
    calc->cursor.y = y;
  }
}

rc_buttonset* rc_create_buttonset(rc_calculator* calc){
  calc->buttonset = malloc(sizeof(rc_buttonset));
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Allocated %d at %p for buttonset", sizeof(rc_buttonset), calc->buttonset); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);
  memset(calc->buttonset, 0, sizeof(rc_buttonset));
  calc->buttonset->curx = POSITION_BUTTONS_X;
  calc->buttonset->cury = POSITION_BUTTONS_Y;
  return calc->buttonset;
}

void rc_add_button(rc_calculator* calc, char* label, int type, int value){
  calc->buttonset->buttons[calc->buttonset->count] = malloc(sizeof(rc_button));
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Allocated %d at %p for button %s", sizeof(rc_calculator), calc, label); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);
  memset(calc->buttonset->buttons[calc->buttonset->count], 0, sizeof(rc_button));
  calc->buttonset->buttons[calc->buttonset->count]->value = value;
  calc->buttonset->buttons[calc->buttonset->count]->type = type;
  calc->buttonset->buttons[calc->buttonset->count]->top = calc->buttonset->cury;
  calc->buttonset->buttons[calc->buttonset->count]->left = calc->buttonset->curx;
  int tlid = getTextlayer(GRect(
    calc->buttonset->curx, calc->buttonset->cury,
    calc->buttonset->curx+SIZE_BUTTON_X, calc->buttonset->cury+SIZE_BUTTON_Y
  ));
  int bufid = getBuffer();
  calc->buttonset->buttons[calc->buttonset->count]->tlid = tlid;
  calc->buttonset->buttons[calc->buttonset->count]->bufid = bufid;
  strcpy(_buffers[bufid], label);
  text_layer_set_font(_textlayers[tlid], fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  //text_layer_set_text_alignment(_textlayers[tlid], GTextAlignmentCenter);  
  //text_layer_set_text(_textlayers[tlid], "*");//debug
  text_layer_set_text(_textlayers[tlid], _buffers[bufid]);
  layer_add_child(window_get_root_layer(_windows[calc->winid]), text_layer_get_layer(_textlayers[tlid]));
  
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Placed button at %d, %d", calc->buttonset->curx, calc->buttonset->cury); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);

  
  // increment offsets
  calc->buttonset->count++;
  calc->buttonset->curx += SIZE_BUTTON_X;
  if(calc->buttonset->curx + SIZE_BUTTON_X > SCREEN_WIDTH){
    calc->buttonset->curx = POSITION_BUTTONS_X;
    calc->buttonset->cury += SIZE_BUTTON_Y;
  }
}

void rc_add_buttons(rc_calculator* calc){
  //int DEBUG = 1;//debug
  rc_add_button(calc, "7", BUTTON_TYPE_NUMBER, 7);
  rc_add_button(calc, "8", BUTTON_TYPE_NUMBER, 8);
  rc_add_button(calc, "9", BUTTON_TYPE_NUMBER, 9);
  rc_add_button(calc, "/", BUTTON_TYPE_FUNCTION, BUTTON_FUNCTION_DIV);
  rc_add_button(calc, "*", BUTTON_TYPE_FUNCTION, BUTTON_FUNCTION_MUL);
  rc_add_button(calc, "-", BUTTON_TYPE_FUNCTION, BUTTON_FUNCTION_SUB);

  rc_add_button(calc, "4", BUTTON_TYPE_NUMBER, 4);
  rc_add_button(calc, "5", BUTTON_TYPE_NUMBER, 5);
  rc_add_button(calc, "6", BUTTON_TYPE_NUMBER, 6);
  rc_add_button(calc, "C", BUTTON_TYPE_FUNCTION, BUTTON_FUNCTION_CLE);
  rc_add_button(calc, "<-", BUTTON_TYPE_FUNCTION, BUTTON_FUNCTION_BAC);
  rc_add_button(calc, "+", BUTTON_TYPE_FUNCTION, BUTTON_FUNCTION_ADD);

  rc_add_button(calc, "1", BUTTON_TYPE_NUMBER, 1);
  rc_add_button(calc, "2", BUTTON_TYPE_NUMBER, 2);
  rc_add_button(calc, "3", BUTTON_TYPE_NUMBER, 3);
  rc_add_button(calc, "0", BUTTON_TYPE_NUMBER, 0);
  rc_add_button(calc, ".", BUTTON_TYPE_FUNCTION, BUTTON_FUNCTION_DOT);
  rc_add_button(calc, "=", BUTTON_TYPE_FUNCTION, BUTTON_FUNCTION_EQU);
}

/**
Notes to self:
As a watch is rotated around its center of mass, the acceleration due to gravity
shifts between the component axes.  This requires establishing a baseline which
corresponds to the way the watch is being held, and then tracking the rotation
of the watch with respect to the baseline.

One Approach:
1. determine the baseline vector (gravity in the current orientation)
2. measure the vector over time
3. direction of the cursor movement is in the downward angle of the intersection of the zero-centered planes
   defined by the two vectors.

Another Approach:
1. keep it flat, smart guy.

 */

void update_vdiff(){  
  //vdiff.x = vcurr.x - vbase.x;
  //vdiff.y = vcurr.y - vbase.y;
  //vdiff.z = vcurr.z - vbase.z;
  vdiff.x = rc_get_smoother_value(&(vfast.sx)) - rc_get_smoother_value(&(vslow.sx));
  vdiff.y = rc_get_smoother_value(&(vfast.sy)) - rc_get_smoother_value(&(vslow.sy));
  vdiff.z = rc_get_smoother_value(&(vfast.sz)) - rc_get_smoother_value(&(vslow.sz));
}

void update_tilt(){
  update_vdiff();
  if(abs(vdiff.x) > THRESH_TILT_X){
    if(abs(vdiff.y) > THRESH_TILT_Y){
      if(vdiff.x < 0){
        if(vdiff.y < 0){
          tilt=1;
        } else {
          tilt=7;
        }
      } else {
        if(vdiff.y < 0){
          tilt=3;
        } else {
          tilt=9;
        }
      }
    } else {
      if(vdiff.x < 0) tilt=4;
      else tilt=6;
    }
  } else if (abs(vdiff.y) > THRESH_TILT_Y){
      if(vdiff.y < 0) tilt=2;    
      else tilt=8;
  } else {
    tilt = 5;
  }
}

/**
Note: if the sampler stops getting called then it's probably time for a watch reboot.
 */
void rc_handle_sampler(AccelData *data, uint32_t num_samples){ 
  if(shutdown) return;
  // get start time
  time_t ts0;
  uint16_t tms0;
  time_ms(&ts0, &tms0);  
  rc_update_smoothvector3(&vslow, data, num_samples);
  rc_update_smoothvector3(&vfast, data, num_samples);
  update_tilt();
  if(data[num_samples-1].timestamp - tilt_time > THRESH_TIME){
    update_cursor();
    tilt_time = data[num_samples-1].timestamp;
  }
  // get end time and update scan time
  time_t ts1;
  uint16_t tms1;
  time_ms(&ts1, &tms1);  
  if(ts1 > ts0){
    tms1 += 1000 * (ts1-ts0);
  }
  scan_time = tms1 - tms0;
  if(num_samples > 0){
    //int s = 10; // scale factor
    //snprintf(tl_debug_buf, 256, "x%dy%dz%d  tilt %d %d", data[0].x/s, data[0].y/s, data[0].z/s, tilt, scan_time);
    snprintf(tl_debug_buf, 256, "x%dy%dz%d  tilt %d ", vdiff.x, vdiff.y, vdiff.z, tilt);
    strcat(tl_debug_buf, tl_debug_buf2);
    if(DEBUG) text_layer_set_text(tl_debug, tl_debug_buf);
  } else {
    snprintf(tl_debug_buf, 256, "not enough samples");
    text_layer_set_text(tl_debug, tl_debug_buf);
  }
}

void rc_add_cursor(rc_calculator* calc){
  int x = POSITION_BUTTONS_X;
  int y = POSITION_BUTTONS_Y;
  int invid = getInvlayer(GRect(x, y, SIZE_BUTTON_X, SIZE_BUTTON_Y));
  layer_add_child(window_get_root_layer(_windows[calc->winid]), inverter_layer_get_layer(_invlayers[invid]));
  calc->cursor.x = x;
  calc->cursor.y = y;
  calc->cursor.invid = invid;
}

rc_button* rc_get_current_button(){
  for(int i = 0; i < calc->buttonset->count; i++){
    if(calc->cursor.y == calc->buttonset->buttons[i]->top && calc->cursor.x == calc->buttonset->buttons[i]->left){
      return calc->buttonset->buttons[i];
    }
  }
  return NULL;
}

/**
 *  pow() implementation.
 */
float rc_pow(float base, float exp){
  if(exp == 0) return 1;
  return base * rc_pow(base, exp - 1);
}

/**
 *  abs() implementation.
 */
float rc_abs(float n){
  if(n < 0) return -n;
  return n;
}

/**
 * Return the value of the current number display.
 */
float rc_get_number_value(){
  float val = 0;
  char* num = _buffers[calc->bufid_num];
  int len = strlen(num);
  int dot = 9999; // a huge number, longer than any buffer could be.
  bool neg = false;
  for(int i = 0; i < len; i++){
    if(num[i] == '.') dot = i;
  }
  int end = (dot < len) ? dot : len;
  for(int i = 0; i < end; i++){ // process integer part
    if(num[i] == '-'){
      neg = true;
    } else {
      // 0 is ascii 48
      float cval = (int)(num[i]) - 48;
      val += cval * rc_pow(10, end - i - 1);
    }
  }
  if(end == dot){ // process fractional part
    for(int i = dot+1; i < len; i++){
      float cval = (int)(num[i]) - 48;
      val += cval / rc_pow(10, i - dot);
    }
  }
  if(neg){
    val = -val;
  }
  return val;
}

#define FRACTION_DIGITS 6 // must be same as number of zeroes in mulitplier
#define FRACTION_MULTIPLIER 1000000

/**
 *  Format float for the number display using only integer formatting.
 */
void rc_update_number_buffer(float value){
  snprintf(_buffers[calc->bufid_num], 256, "%d", (int)value);
  if((int)value != value){ // need to add decimal part
    // this method adds a max of six decimal places, then cuts off trailing zeroes.
    char buf[256];
    char zbuf[32];
    memset(zbuf, '0', 32);
    float frac = value - (int)value;
    frac *= FRACTION_MULTIPLIER; // this multiplier determines max number of decimals
    snprintf(buf, 256, "%d", (int)frac);
    int zlen = FRACTION_DIGITS - strlen(buf); // zlen is the length before zero-padding
    for(int i = strlen(buf) - 1; i > -1; i--){
      if(buf[i] == '0'){
        buf[i] = 0;
      }
    }
    memcpy(zbuf + zlen, buf, strlen(buf) + 1);
    strcpy(buf, zbuf);
    strcat(_buffers[calc->bufid_num], ".");
    strcat(_buffers[calc->bufid_num], buf);
  }
}

void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  rc_button* button = rc_get_current_button();
  if(button != NULL){    
    if(button->type == BUTTON_TYPE_NUMBER){
      if(calc->mode == CALC_MODE_NEWOP){
        calc->tmpval = 0;
        calc->tmpop = 0;
        strcpy(_buffers[calc->bufid_op], CALC_OP_INITIAL);
        strcpy(_buffers[calc->bufid_num], CALC_NUM_INITIAL);
        calc->mode = CALC_MODE_INPUT;
      } else if(calc->mode == CALC_MODE_MIDOP){
        strcpy(_buffers[calc->bufid_num], CALC_NUM_INITIAL);
        calc->mode = CALC_MODE_INPUT;
      }
      if(rc_get_number_value() == 0 && _buffers[calc->bufid_num][strlen(_buffers[calc->bufid_num])-1] != '.'){ // replace
        strcpy(_buffers[calc->bufid_num], _buffers[button->bufid]);
      } else { // append
        strcat(_buffers[calc->bufid_num], _buffers[button->bufid]);
      }      
      snprintf(tl_debug_buf2, 256, " %d", (int)rc_get_number_value());//debug
    } else if(button->type == BUTTON_TYPE_FUNCTION){
      float val1 = rc_get_number_value();
      float val0 = 0;
      float res = 0; // result
      if(calc->mode == CALC_MODE_NEWOP){ // then we just got an answer and we want to re-use the old input
        val0 = val1;
        val1 = calc->tmpval;
      } else { // we want to use the new input on an old number
        val0 = calc->tmpval;
      }
      switch(button->value){
        case BUTTON_FUNCTION_EQU:
          if(calc->tmpop > 0){
            switch(calc->tmpop){
              case BUTTON_FUNCTION_ADD:
                res = val0 + val1;
                break;
              case BUTTON_FUNCTION_SUB:
                res = val0 - val1;
                break;
              case BUTTON_FUNCTION_MUL:
                res = val0 * val1;
                break;
              case BUTTON_FUNCTION_DIV:
                if(val1 == 0){
                  res = 0; // TODO: display error
                  //strcpy(_buffers[calc->bufid_num], "E");
                } else {
                  res = val0 / val1;  
                }                
                break;
            }
            //strcpy(_buffers[calc->bufid_op], "");
            calc->tmpval = val1; // so we can chain operations
            rc_update_number_buffer(res);
            calc->mode = CALC_MODE_NEWOP;
          }
          break;
        case BUTTON_FUNCTION_MUL:          
          strcpy(_buffers[calc->bufid_op], _buffers[button->bufid]);
          calc->tmpval = rc_get_number_value();
          calc->tmpop = BUTTON_FUNCTION_MUL;
          //strcpy(_buffers[calc->bufid_num], CALC_VALUE_INITIAL);
          calc->mode = CALC_MODE_MIDOP;
          break;
        case BUTTON_FUNCTION_CLE:
          strcpy(_buffers[calc->bufid_op], CALC_OP_INITIAL);
          strcpy(_buffers[calc->bufid_num], CALC_NUM_INITIAL);
          calc->tmpval = 0;
          break;
        case BUTTON_FUNCTION_BAC:
          //if(rc_get_number_value() != 0){
            if(strlen(_buffers[calc->bufid_num]) > 1){
              _buffers[calc->bufid_num][strlen(_buffers[calc->bufid_num]) - 1] = 0; // truncate the string
            } else {
              strcpy(_buffers[calc->bufid_num], CALC_NUM_INITIAL);
            }
          //}
          break;
        case BUTTON_FUNCTION_DIV:
          strcpy(_buffers[calc->bufid_op], _buffers[button->bufid]);
          calc->tmpval = rc_get_number_value();
          calc->tmpop = BUTTON_FUNCTION_DIV;
          //strcpy(_buffers[calc->bufid_num], "0");
          calc->mode = CALC_MODE_MIDOP;
          break;
        case BUTTON_FUNCTION_DOT: ; // empty statement to allow next line to be a declaraion
          if(calc->mode == CALC_MODE_NEWOP){
            calc->tmpval = 0;
            calc->tmpop = 0;
            strcpy(_buffers[calc->bufid_op], CALC_OP_INITIAL);
            strcpy(_buffers[calc->bufid_num], CALC_NUM_INITIAL);
            calc->mode = CALC_MODE_INPUT;
          } else if(calc->mode == CALC_MODE_MIDOP){
            strcpy(_buffers[calc->bufid_num], CALC_NUM_INITIAL);
            calc->mode = CALC_MODE_INPUT;
          }
          char* dnum = _buffers[calc->bufid_num];
          int dlen = strlen(dnum);
          bool dhas = false;
          for(int i = 0; i < dlen; i++){
            if(dnum[i] == '.') dhas = true;
          }
          if(!dhas){
            strcat(_buffers[calc->bufid_num], ".");            
            calc->mode = CALC_MODE_INPUT;
          }
          break; 
        case BUTTON_FUNCTION_ADD:
          strcpy(_buffers[calc->bufid_op], _buffers[button->bufid]);
          calc->tmpval = rc_get_number_value();
          calc->tmpop = BUTTON_FUNCTION_ADD;
          //strcpy(_buffers[calc->bufid_num], "0");
          calc->mode = CALC_MODE_MIDOP;
          break;
        case BUTTON_FUNCTION_SUB:
          strcpy(_buffers[calc->bufid_op], _buffers[button->bufid]);
          calc->tmpval = rc_get_number_value();
          calc->tmpop = BUTTON_FUNCTION_SUB;
          //strcpy(_buffers[calc->bufid_num], "0");
          calc->mode = CALC_MODE_MIDOP;
          break;
        default:
          APP_LOG(APP_LOG_LEVEL_ERROR, "Button has an invalid value.");
          break;
      }
    } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "Button has an invalid type.");
    }
    text_layer_set_text(_textlayers[calc->tlid_num], _buffers[calc->bufid_num]);
    text_layer_set_text(_textlayers[calc->tlid_op], _buffers[calc->bufid_op]);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Unable to resolve button from cursor.");
  }
}

void layer_update_proc(struct Layer *layer, GContext *ctx){
  /* this gets drawn over by the text layers
  graphics_draw_round_rect(ctx, GRect(
    POSITION_OP_X, POSITION_OP_Y, 
    POSITION_NUM_X + SIZE_NUM_X - POSITION_OP_X, POSITION_NUM_Y + SIZE_NUM_Y - POSITION_OP_Y
  ), 3); // round corners...
  */
  graphics_draw_line(ctx, GPoint(POSITION_BUTTONS_X, POSITION_BUTTONS_Y - 3), GPoint(139, POSITION_BUTTONS_Y - 3));
}

void config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

rc_calculator* rc_create_calculator(rc_calculator* calc){
  init_global_resources();
  calc = malloc(sizeof(rc_calculator));
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Allocated %d at %p for calculator", sizeof(rc_calculator), calc); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);
  memset(calc, 0, sizeof(rc_calculator));
  calc->buttonset = rc_create_buttonset(calc);
  calc->winid = getWindow();
  rc_add_buttons(calc);
  rc_add_cursor(calc);
  layer_set_update_proc(window_get_root_layer(_windows[calc->winid]), layer_update_proc);
  window_stack_push(_windows[calc->winid], true);
  
  // set up operator indicator and number window
  calc->tlid_op = getTextlayer(GRect(POSITION_OP_X, POSITION_OP_Y, SIZE_OP_X, SIZE_OP_Y));
  calc->bufid_op = getBuffer();
  text_layer_set_text(_textlayers[calc->tlid_op], _buffers[calc->bufid_op]);
  text_layer_set_font(_textlayers[calc->tlid_op], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_get_root_layer(_windows[calc->winid]), text_layer_get_layer(_textlayers[calc->tlid_op]));

  calc->tlid_num = getTextlayer(GRect(POSITION_NUM_X, POSITION_NUM_Y, SIZE_NUM_X, SIZE_NUM_Y));
  calc->bufid_num = getBuffer();
  text_layer_set_text(_textlayers[calc->tlid_num], _buffers[calc->bufid_num]);
  text_layer_set_text_alignment(_textlayers[calc->tlid_num], GTextAlignmentRight);
  text_layer_set_font(_textlayers[calc->tlid_num], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_get_root_layer(_windows[calc->winid]), text_layer_get_layer(_textlayers[calc->tlid_num]));
  strcat(_buffers[calc->bufid_num], CALC_NUM_INITIAL);
  
  // subscribe to accelerometer data updates
  accel_data_service_unsubscribe(); // reset?
  accel_data_service_subscribe(4, rc_handle_sampler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_50HZ);
  //accel_service_set_samples_per_update(5);
  
  // setup smoothers
  rc_setup_smoothvector3(&vfast, 4);
  rc_setup_smoothvector3(&vslow, 64);

  // setup click handlers
  window_set_click_config_provider(_windows[calc->winid], config_provider);
  return calc;
}

void rc_destroy_calculator(rc_calculator* calc){
  shutdown = true;
  // unsubscribe from data service
  accel_data_service_unsubscribe();
  // destroy global resources
  for(int i = 0; i < _textlayersn; i++){
    if(_textlayers[i] != NULL){
      if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "rc_destory_calculator: destroy textlayer %d", i); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);
      text_layer_destroy(_textlayers[i]);
    }
  }
  for(int i = 0; i < _invlayersn; i++){
    if(_invlayers[i] != NULL){
      if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "rc_destory_calculator: destroy invlayer %d", i); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);
      inverter_layer_destroy(_invlayers[i]);
    }
  }    
  for(int i = 0; i < _windowsn; i++){
    if(_windows[i] != NULL){
      if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "rc_destory_calculator: destroy window %d", i); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);
      window_destroy(_windows[i]);
    }
  }
  if(calc != NULL){
    if(LOGGING) APP_LOG(APP_LOG_LEVEL_INFO, "rc_destory_calculator: calc not null");
    if(calc->buttonset != NULL){
      if(LOGGING) APP_LOG(APP_LOG_LEVEL_INFO, "rc_destory_calculator: buttonset not null");
      int i;
      for(i = 0; i < calc->buttonset->count; i++){
        if(calc->buttonset->buttons[i] != NULL){
          if(LOGGING) APP_LOG(APP_LOG_LEVEL_INFO, "rc_destory_calculator: free button");
          free(calc->buttonset->buttons[i]);
        }
      }
      if(LOGGING) APP_LOG(APP_LOG_LEVEL_INFO, "rc_destory_calculator: free buttonset");
      free(calc->buttonset);
    } else {
      if(LOGGING) APP_LOG(APP_LOG_LEVEL_INFO, "rc_destory_calculator: calc->buttonset is null");
    }
    if(LOGGING) APP_LOG(APP_LOG_LEVEL_INFO, "rc_destory_calculator: free calculator");
    free(calc);
  } else {
    if(LOGGING) APP_LOG(APP_LOG_LEVEL_INFO, "rc_destory_calculator: calc is null");
  }
}

int main(void) {
  //debug display
  tl_debug = text_layer_create(GRect(0, 132, 144, 22));
  memset(tl_debug_buf, 0, 256);
  strcat(tl_debug_buf, "hello debug!");
  if(DEBUG) text_layer_set_text(tl_debug, tl_debug_buf);
  
  // initialize the calculator
  calc = rc_create_calculator(calc);
  if(LOGGING) snprintf(buf_debug, BUF_SIZE_DEBUG, "Allocated %d at %p for calculator", sizeof(rc_calculator), calc); APP_LOG(APP_LOG_LEVEL_INFO, buf_debug);
 
  layer_add_child(window_get_root_layer(_windows[0]), text_layer_get_layer(tl_debug));
  //tltest = text_layer_create(GRect(10,10,36,36)); text_layer_set_text(tltest, "!");//works
  //int tltest = getTextlayer(GRect(10,10,36,36)); text_layer_set_text(_textlayers[tltest], "!");//works
  //layer_add_child(window_get_root_layer(_windows[0]), text_layer_get_layer(_textlayers[tltest]));
  //layer_add_child(window_get_root_layer(_windows[0]), text_layer_get_layer(_textlayers[0]));//doesn't work  
  
  if(LOGGING) APP_LOG(APP_LOG_LEVEL_INFO, "starting event loop");
  app_event_loop();
  rc_destroy_calculator(calc);
}
