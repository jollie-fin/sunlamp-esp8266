#include <Esp.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP_EEPROM.h>
#include <stdint.h>
#include <cmath>
#include <algorithm>
#include <inttypes.h>
#include <ArduinoOTA.h>
#include <Print.h>

#define GAMMA_COEFF 2.8
#define HOT_LED D2
#define COLD_LED D1
#define SWITCH D5

#define DEBOUNCE_DELAY_MS 100

#define XSTR(A) #A
#define STR(A) XSTR(A)

#define PWM_RANGE 1000
#define HOT_RANGE 1000
#define HOT_COEFF (HOT_RANGE / PWM_RANGE)
#define COLD_RANGE 1000
#define COLD_COEFF (COLD_RANGE / PWM_RANGE)
#define TOTAL_GAMMA_RANGE (HOT_RANGE + COLD_RANGE)
typedef uint16_t gamma_light_t;

#define MAX_USER_LIGHT 200
typedef uint8_t user_light_t;

#define LINEAR_RANGE 20000
typedef uint16_t linear_light_t;

#define FREE_TRANSITION_SPEED 5000

#define SECONDS_PER_DAY 86400
#define EPOCH_DAY 3 // 1 january 1970 was a thursday. 0 is monday
#define EVENTS_COUNT 6

#define LIGHT_SHAPE_COUNT 4
struct light_datapoint_t
{
  uint32_t offset;
  user_light_t power;
};

struct __attribute__((packed)) event_t
{
  user_light_t power;
  uint8_t ramp_in;
  uint8_t hold;
  uint8_t ramp_out;
  uint32_t time : 17;
  uint8_t days : 7;
  char name[9];
};

ESP8266WebServer server(80);

template <size_t BUFFER_SIZE>
class SuperSerial : public Print
{
public:
  using Print::Print;
  size_t write(const uint8_t *buffer, size_t size) override; // Overriding base functionality
  size_t write(uint8_t) override;

  void send();

protected:
  char buffer_print[BUFFER_SIZE];
  char *buffer_ptr = buffer_print;
};

SuperSerial<4096> superSerial;

linear_light_t userToLin(user_light_t light)
{
  int32_t retval = light;
  retval *= LINEAR_RANGE;
  retval /= MAX_USER_LIGHT;
  retval = std::min(retval, LINEAR_RANGE);
  superSerial.printf("userToLin(%" PRIu8 ") = %" PRIi32 "\n", light, retval);
  return retval;
}

user_light_t linToUser(linear_light_t light)
{
  int32_t retval = light;
  retval *= MAX_USER_LIGHT;
  retval /= LINEAR_RANGE;
  retval = std::min(retval, MAX_USER_LIGHT);
  //  superSerial.printf("linToUser(%" PRIu16 ") = %" PRIi32 "\n", light, retval);
  return retval;
}

#define MS_IN_MIN 60000
#define UPDATE_EEPROM_MS (10 * MS_IN_MIN)

// Replace with your network credentials
const char *ssid_ap = "speedomobile";
const char *password_ap = "***REMOVED***";

const char *ssid = "***REMOVED***";
const char *password = " ***REMOVED *** ";

const char *lamp_name = "sunlamp";

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void decode_data(char *data);

void handle_switch();

linear_light_t get_light_goal();
void transition_to(user_light_t next_light, uint32_t transition_ms);
void transition_with_shape(light_datapoint_t light_shape[], int light_shape_count);
void update_light();
bool is_in_transition();
bool is_millis_increasing(uint32_t before, uint32_t after);
linear_light_t get_light_level();
void set_light_level(linear_light_t light);

void init_eeprom();
void setup_eeprom();
void update_eeprom();
void set_event(const event_t &event, uint8_t no_event);
const event_t &get_event(uint8_t no_event);
const event_t *get_current_event(unsigned long current_epoch);

void send_print_to_client();

/////////////////////////////// HTML //////////////////////////////////

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Lamp</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
  html {
    font-family: Arial, Helvetica, sans-serif;
    text-align: center;
  }
  body {
    margin: 0;
  }
  p {
    margin: 0px 0px 0px 0px;
  }
  .fieldset-auto-width {
     display: inline-block;
  }
    
  .aligned {
      display: table;
      align: left;
      margin-left: auto;
      margin-right: auto;
  }
  .event {
      border: 1px solid black;
  }
  .aligned > .labelled {
      display: table-row;
  }
  .aligned > .labelled > * {
      display: table-cell;
      padding: 10px;
      text-align : left;
  }
  .slider_output {
      width: 2em;
  }
  .aligned > .labelled > label {
      text-align : right;
  }
  button {
    margin: .1rem;
    padding: 15px 20px;
    font-size: 24px;
    text-align: center;
    outline: none;
    color: #fff;
    background-color: #0f8b8d;
    border: none;
    border-radius: 5px;
    -webkit-touch-callout: none;
    -webkit-user-select: none;
    -khtml-user-select: none;
    -moz-user-select: none;
    -ms-user-select: none;
    user-select: none;
    -webkit-tap-highlight-color: rgba(0,0,0,0);
   }
   /*.button:hover {background-color: #0f8b8d}*/
   button:active {
     background-color: #0f8b8d;
     box-shadow: 2 2px #CDCDCD;
     transform: translateY(2px);
   }
  </style>
<title>Sun Lamp</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
</head>
<body>
  <div class="content">
    <h1>Time</h1>
    <p id="time"></p>
    <p id="weekday"></p>
    <h1>Controls</h1>
    <div class="aligned">
      <div class="labelled targetselector">
  <label for="targetPower">Target power</label>
  <input type="range" min="0" max=")rawliteral" STR(MAX_USER_LIGHT) R"rawliteral(" value="0" class="powerslider" id="targetPower" value="0">
      </div>
      <div class="labelled">
  <label for="currentPower">Current power</label>
  <input type="range" min="0" max=")rawliteral" STR(MAX_USER_LIGHT) R"rawliteral(" value="0" class="powerslider" id="currentPower" value="0" disabled>
      </div>
    </div>

    <h1>Events</h1>
    <div id="events"></div>
  </div>
<script>

const events_count = )rawliteral" STR(EVENTS_COUNT) R"rawliteral(

function range(size) {
  return Object.keys([...Array(size)]);
}

function create_element(tagname, name, attributes, children = [])
{
    const content = document.createElement(tagname);
    for (const [key, value] of Object.entries(attributes))
      content.setAttribute(key, value);
    for (const child of children)
      content.appendChild(child);

    if ('id' in attributes && name) {
      const label = document.createElement("label");
      label.for = attributes.id;
      label.innerHTML = name;
    
      const retval = document.createElement("div");
      retval.className = "labelled";
      retval.appendChild(label);
      retval.appendChild(content);
      return retval;
    }
   
    return content;
}

async function get_log()
{
  const path = new URL(`${window.location.href}log`);
  const response = await fetch(path);
  if (!response.ok) {
      console.log("Invalid response on ajax call : ", {path, response});
      return;
  }
  if (response.status == 200) {
      const text = await response.text();
      text.split('\n').map(a => {if (a) console.log(`>>> ${a}`)});
      return;
  }
}


async function ajax(parameters)
{
  const path = new URL(`${window.location.href}ajax`);
  for (const [key, value] of Object.entries(parameters)) {
    path.searchParams.set(key, value);
  }
  const response = await fetch(path);
  if (!response.ok) {
      console.log("Invalid response on ajax call : ", {path, response});
      return undefined;
  }
  if (response.status == 200) {
      return response.json();
  }
  else
      return undefined;
}

async function create_event_boxes(nb_boxes)
{
    const container = document.getElementById("events");
    for (const i of range(nb_boxes)) {
        const time_element = create_element("input","Time of the event", {id : `timestamp${i}`, type: "time", class: "timeinput", value: "13:00"});
        const power = create_element("input","Brightness", {id : `target${i}`, type: "range", min: 0, max: )rawliteral" STR(MAX_USER_LIGHT) R"rawliteral(, class: "powerslider"})
        const ramp_in = create_element("input","Ramp in (min)", {id : `ramp_in${i}`, type: "range", min: 0, max: 255, value: 15, class: "timeslider"})
        const hold = create_element("input","Hold (min)", {id : `hold${i}`, type: "range", min: 0, max: 255, value: 5, class: "timeslider"})
        const ramp_out = create_element("input","Ramp out (min)", {id : `ramp_out${i}`, type: "range", min: 0, max: 255, value: 15, class: "timeslider"})
        const days = create_element("div", "Day of the week", {id : `days${i}`, class: "days"},
                  range(7).map(x => create_element("input", undefined, {id : `day${i}-${x}`, type: "checkbox"})));
        const box = create_element("div", undefined, {class: "aligned"}, [time_element, power, ramp_in, hold, ramp_out, days]);
        const name = create_element("input", undefined, {id: `event_name${i}`, class: "event_name"});
        const legend = create_element("legend", undefined, {class: "legend"}, [name]);
        const event = create_element("fieldset", undefined, {id: `event${i}`, class: "event fieldset-auto-width", "data-idx": i}, [legend, box]);
        const outer = create_element("div", undefined, {}, [event]);
        container.appendChild(outer);
    }
}
 
async function add_output_to_range()
{
  Array.from(document.querySelectorAll("input[type=range]:not([disabled])")).map(elt =>
  {
      const output = create_element("input", undefined, {type: "text", class: "slider_output"});
      elt.parentElement.insertBefore(output, elt.nextSibling);
      elt.addEventListener("input", () => output.value = elt.value);
      elt.dataset.output = output;
      output.addEventListener("input", () => {
        if (elt.hasAttribute("min") && +output.value < +elt.min)
          output.value = elt.min;
        if (elt.hasAttribute("max") && +output.value > +elt.max)
          output.value = elt.max;
        elt.value = output.value;
      });
      output.value = elt.value;
  });
}

async function notifyEvent(i)
{
  const get_elt = name => document.getElementById(`${name}${i}`);
  const get_value = name => get_elt(name).value;

  const payload = {
    time : timeToTimestamp(get_value('timestamp')),
    power : get_value('target'),
    ramp_in : get_value('ramp_in'),
    hold : get_value('hold'),
    ramp_out : get_value('ramp_out'),
    name : get_value('event_name'),
    id : i,
    days : Array.from(document.querySelectorAll(`#days${i} > input[type=checkbox]`)).sort((a,b)=>(a.id > b.id)).map(x => x.checked ? "X" : ".").join(""),
    name : get_value('event_name').replace(";","").replace(":","").replace(" ","")
  };

  await ajax(payload);
}

async function notifyTarget()
{
  return ajax({goal:document.getElementById("targetPower").value});
}

async function add_event_listeners()
{    
    Array.from(document.getElementsByClassName("event")).map(event => {
      const callback = () => notifyEvent(event.dataset.idx);
      Array.from(event.getElementsByTagName("input")).map(input => input.addEventListener("input", callback));
    });

    Array.from(document.querySelectorAll(".targetselector > input")).map(input =>
      input.addEventListener("input", notifyTarget));
}

async function onLoad() {
  create_event_boxes(events_count);
  add_output_to_range();
  add_event_listeners();
  await updateRTC();
  await updateEvents();
  setTimeout(async function repeat() {
    await updateRTC();
    setTimeout(repeat, 1000);
  }, 1000);


  setTimeout(async function repeat() {
    await updateEvents();
    setTimeout(repeat, 10000);
  }, 10000);

  setTimeout(async function repeat() {
    await get_log();
    setTimeout(repeat, 1000);
  }, 1000);

}

window.addEventListener('load', onLoad);

function timestampToTime(timestamp) {
  return [timestamp/3600, timestamp/60%60, timestamp%60].map((value,idx)=>Math.floor(value).toString().padStart(2,"0")).join(":");
}

function timeToTimestamp(time) {
  return time.split(':').map((value,idx)=>value * Math.pow(60, 2-idx)).reduce((a,b)=>a+b);
}


async function updateRTC() {
  const data = await ajax({get_current:''});
  if ('power' in data)
    document.getElementById('currentPower').value = data.power;
  if ('goal' in data)
    Array.from(document.querySelectorAll('.targetselector > input')).map(elt => elt.value = data.goal);
  if ('time' in data)
    document.getElementById('time').innerHTML = data.time;
  if ('weekday' in data)
    document.getElementById('weekday').innerHTML = data.weekday;
}

async function updateEvents() {
  for (const i of range(events_count)) { 
    
    const get_elt = name => document.getElementById(`${name}${i}`);
    const set_value = (name, value) =>
    {
      const elt = get_elt(name);
      elt.value = value;
      if (elt.nextSibling)
        elt.nextSibling.value = value;
    }

    const event = await ajax({get_event:i});
    set_value(`timestamp`, timestampToTime(event.time));
    set_value(`target`, event.power);
    set_value(`ramp_in`, event.ramp_in);
    set_value(`hold`, event.hold);
    set_value(`ramp_out`, event.ramp_out);
    set_value(`event_name`, event.name);
    event.days.map((x, index) => document.getElementById(`day${i}-${index}`).checked = x);
  }
}

 
</script>
</body>
</html>

)rawliteral";

////////////// Websocket:send_message ///////////////////

auto boolToStr(bool bla)
{
  return bla ? F("true") : F("false");
}
void ajaxEvent(int i)
{
  if (i >= EVENTS_COUNT || i < 0)
  {
    server.send(404, "text/json", "\"unknown event\"");
    return;
  }

  const event_t &event = get_event(i);
  static char str[180];
  static char buffer_name[sizeof(event.name)];
  snprintf(buffer_name, sizeof(buffer_name), "%s", event.name);

  snprintf_P(
      str,
      sizeof(str),
      PSTR("{\"id\":%d,\"days\":[%s,%s,%s,%s,%s,%s,%s],\"time\":%" PRIu32 ",\"ramp_in\":%d,\"hold\":%d,\"ramp_out\":%d,\"power\":%d,\"name\":\"%s\"}"),
      i,
      boolToStr(event.days & (1u << 0)),
      boolToStr(event.days & (1u << 1)),
      boolToStr(event.days & (1u << 2)),
      boolToStr(event.days & (1u << 3)),
      boolToStr(event.days & (1u << 4)),
      boolToStr(event.days & (1u << 5)),
      boolToStr(event.days & (1u << 6)),
      event.time,
      static_cast<int>(event.ramp_in),
      static_cast<int>(event.hold),
      static_cast<int>(event.ramp_out),
      static_cast<int>(event.power),
      buffer_name);
  server.send(200, "text/json", str);
}
const char *get_weekday()
{
  switch (timeClient.getDay())
  {
  case 1:
    return PSTR("Monday");
  case 2:
    return PSTR("Tuesday");
  case 3:
    return PSTR("Wednesday");
  case 4:
    return PSTR("Thursday");
  case 5:
    return PSTR("Friday");
  case 6:
    return PSTR("Saturday");
  case 0:
    return PSTR("Sunday");
  default:
    return PSTR("");
  }
}
void ajaxPower()
{
  static char str[100];
  snprintf_P(str,
             sizeof(str),
             PSTR("{\"power\":%d,\"goal\":%d,\"time\":\"%s\",\"weekday\":\"%s\"}"),
             linToUser(get_light_level()),
             linToUser(get_light_goal()),
             timeClient.getFormattedTime().c_str(),
             get_weekday());
  server.send(200, "text/json", str);
}

/////////// Websocket:decode_message //////////////

uint32_t parse_uint(const String &data, int bit_size)
{
  uint32_t retval;
  sscanf(data.c_str(), "%" SCNu32, &retval);

  return std::min(retval, (1u << bit_size) - 1);
}

int32_t parse_int(const String &data, int bit_size)
{
  int32_t retval;
  sscanf(data.c_str(), "%" SCNi32, &retval);
  return std::min(std::max(retval, -(1 << (bit_size - 1))), (1 << (bit_size - 1)) - 1);
}

void handleRoot()
{
  server.send_P(200, "text/html", index_html);
}

void handleAjax()
{
  int id = -1;
  event_t event = {};
  int nb_fields = 0;

  for (int i = 0; i < server.args(); i++)
  {
    const String &key = server.argName(i);
    const String &data = server.arg(i);
    if (key == F("events_count"))
    {
      server.send(200, "text/json", String(EVENTS_COUNT));
      return;
    }
    else if (key == F("get_current"))
    {
      ajaxPower();
      return;
    }
    else if (key.startsWith(F("get_event")))
    {
      ajaxEvent(data.toInt());
      return;
    }
    else if (key == F("id"))
    {
      id = data.toInt();
      nb_fields++;
    }
    else if (key == F("time"))
    {
      event.time = parse_uint(data, 17);
      nb_fields++;
    }
    else if (key == F("ramp_in"))
    {
      event.ramp_in = parse_uint(data, 8);
      nb_fields++;
    }
    else if (key == F("ramp_out"))
    {
      event.ramp_out = parse_uint(data, 8);
      nb_fields++;
    }
    else if (key == F("hold"))
    {
      event.hold = parse_uint(data, 8);
      nb_fields++;
    }
    else if (key == F("power"))
    {
      event.power = parse_uint(data, 8);
      nb_fields++;
    }
    else if (key == F("name"))
    {
      char *dst = event.name;
      for (unsigned i = 0; i < sizeof(event.name); i++)
      {
        char c = data[i];
        if (!c)
          break;
        if (!isalpha(c) && !isdigit(c) && c != '.' && c != ' ' && c != '_')
        {
          break;
        }
        *dst++ = c;
      }
      if (static_cast<unsigned>(dst - event.name) < sizeof(event.name))
        *dst = '\0';
      nb_fields++;
    }
    else if (key == F("days"))
    {
      if (data.length() != 7)
      {
        superSerial.print(F("Impossible to parse days : "));
        superSerial.println(data);
        continue;
      }
      for (int i = 0; i < 7; i++)
      {
        event.days |= (!strchr(" .OoNnFf", data[i])) << i;
      }
      nb_fields++;
    }
    else if (key == F("goal"))
    {
      superSerial.printf("Power : %i\n", (int)parse_uint(data, 8));

      transition_to(parse_uint(data, 8), FREE_TRANSITION_SPEED);
    }
    else
    {
      superSerial.print(F("Unrecognized key : "));
      superSerial.println(key);
      continue;
    }
  }
  if (nb_fields)
  {
    if (nb_fields != 8 || id < 0 || id >= EVENTS_COUNT)
    {
      superSerial.println(F("Impossible to decode event"));
      server.send(404, "text/json", "null");
      return;
    }
    set_event(event, id);
  }
  server.send(204, "text/json", "null");
  superSerial.println(F("notify"));
}

/////////////////////////////// Setup ////////////////////////////////////////

void setup()
{
  delay(1000);
  Serial.begin(115200);
  // setup_eeprom();
  init_eeprom();

  pinMode(SWITCH, INPUT_PULLUP);
  pinMode(HOT_LED, OUTPUT);
  pinMode(COLD_LED, OUTPUT);
  analogWriteRange(PWM_RANGE);

  superSerial.print(F("Event size :"));
  superSerial.println(sizeof(event_t));

  // Connect to Wi-Fi network with SSID and password
  superSerial.println(F(""));
  superSerial.println(F("Connecting to "));
  superSerial.println(ssid);
  superSerial.println(password);

  int max_trial = 50;
  superSerial.print("Trying for ");
  superSerial.print(max_trial / 2);
  superSerial.print("s :");
  WiFi.begin(ssid, password);

  int trial = 0;
  while (WiFi.status() != WL_CONNECTED && trial < max_trial)
  {
    delay(500);
    trial++;
    superSerial.print(".");
  }
  superSerial.println("");

  IPAddress myIP;
  if (trial == max_trial)
  {
    superSerial.println("Unable. Starting own access point");
    IPAddress apIP(10, 10, 10, 10);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid_ap, password_ap);
    myIP = WiFi.softAPIP();
  }
  else
  {
    myIP = WiFi.localIP(); // Send the IP address of the ESP8266 to the computer
  }

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(lamp_name);

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    superSerial.println("Start updating " + type); });
  ArduinoOTA.onEnd([]()
                   { superSerial.println(F("\nEnd")); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { superSerial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    superSerial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      superSerial.println(F("Auth Failed"));
    } else if (error == OTA_BEGIN_ERROR) {
      superSerial.println(F("Begin Failed"));
    } else if (error == OTA_CONNECT_ERROR) {
      superSerial.println(F("Connect Failed"));
    } else if (error == OTA_RECEIVE_ERROR) {
      superSerial.println(F("Receive Failed"));
    } else if (error == OTA_END_ERROR) {
      superSerial.println(F("End Failed"));
    } });
  ArduinoOTA.begin();
  /*
  if (!MDNS.begin(lamp_name)) {             // Start the mDNS responder for esp8266.local
    superSerial.println(F("Error setting up MDNS responder!"));
  } else {
    MDNS.addService("http", "tcp", 80);
    superSerial.print(F("mDNS responder started : "));
    superSerial.println(lamp_name);
  }
*/
  server.on("/", handleRoot);
  server.on("/ajax", handleAjax);
  server.on("/log", []()
            { superSerial.send(); });

  server.begin();

  timeClient.begin();
  timeClient.setTimeOffset(3 * 3600);
}

////////////////////////////// loop /////////////////////////////////

void loop()
{
  server.handleClient();
  ArduinoOTA.handle();

  MDNS.update();
  update_light();
  //  update_eeprom();
  timeClient.update();

  handle_switch();

  if (is_in_transition())
    return;

  const event_t *event = get_current_event(timeClient.getEpochTime());
  if (!event)
  {
    return;
  }

  superSerial.printf(PSTR("triggering event %s\n"), event->name);

  light_datapoint_t shape[3];
  shape[0].offset = event->ramp_in * MS_IN_MIN;
  shape[0].power = event->power;
  shape[1].offset = event->ramp_in * MS_IN_MIN + event->hold * MS_IN_MIN;
  shape[1].power = event->power;
  shape[2].offset = event->ramp_in * MS_IN_MIN + event->hold * MS_IN_MIN + event->ramp_out * MS_IN_MIN;
  shape[2].power = 0;
  superSerial.println(F("registering transition"));
  transition_with_shape(shape, 3);
  superSerial.println(F("registered"));
}

/////////////////// REMANENT BEHAVIOUR //////////////////////////

struct eeprom_state_t
{
  event_t events[EVENTS_COUNT];
} g_eeprom_state = {};

bool g_eeprom_state_dirty = false;

void init_eeprom()
{
  g_eeprom_state.events[0] = event_t{.power = 60, .ramp_in = 20, .hold = 40, .ramp_out = 30, .time = 21 * 3600 + 10 * 60, .days = 0x7F, .name = "bisounou"};
  g_eeprom_state.events[1] = event_t{.power = 200, .ramp_in = 20, .hold = 40, .ramp_out = 10, .time = 6 * 3600 + 30 * 60, .days = 0x7F, .name = "cocorico"};
}

void setup_eeprom()
{
  EEPROM.begin(sizeof(eeprom_state_t));
  EEPROM.get(0, g_eeprom_state);
}

void update_eeprom()
{
  static uint32_t last_eeprom_update = 0;
  if (millis() - last_eeprom_update > UPDATE_EEPROM_MS && g_eeprom_state_dirty)
  {
    EEPROM.put(0, g_eeprom_state);
    EEPROM.commit();
    last_eeprom_update = millis();
    g_eeprom_state_dirty = false;
  }
}

void set_event(const event_t &event, uint8_t no_event)
{
  if (no_event > EVENTS_COUNT)
  {
    return;
  }
  g_eeprom_state.events[no_event] = event;
  g_eeprom_state_dirty = true;
}

const event_t &get_event(uint8_t no_event)
{
  return g_eeprom_state.events[no_event];
}

const event_t *get_current_event(unsigned long current_epoch)
{
  int day_of_week = ((current_epoch / SECONDS_PER_DAY) + EPOCH_DAY) % 7;
  uint32_t seconds_in_day = current_epoch % SECONDS_PER_DAY;
  for (int i = 0; i < EVENTS_COUNT; i++)
  {
    const event_t &event = g_eeprom_state.events[i];
    if (!(event.days & (1 << day_of_week)))
    {
      continue;
    }
    uint32_t start_timestamp = event.time;
    uint32_t end_timestamp = start_timestamp + event.ramp_in * 60;
    uint32_t timestamp = seconds_in_day < start_timestamp ? seconds_in_day + SECONDS_PER_DAY : seconds_in_day;
    if (timestamp > end_timestamp)
    {
      continue;
    }
    return &event;
  }
  return NULL;
}

//////////////// HANDLE SWITCH LEVER ////////////////////

void handle_switch()
{
  static uint32_t last_switch_time = millis();
  static int32_t last_button_state = digitalRead(SWITCH);
  static int32_t button_state = last_button_state;

  int32_t current_state = digitalRead(SWITCH);
  uint32_t switch_time = millis();
  if (current_state != last_button_state)
  {
    last_switch_time = switch_time;
  }
  last_button_state = current_state;

  if (switch_time - last_switch_time > DEBOUNCE_DELAY_MS)
  {
    if (current_state != button_state)
    {
      superSerial.println(current_state);
      if (get_light_level())
      {
        superSerial.println("Goodnight");
        transition_to(0, FREE_TRANSITION_SPEED);
      }
      else
      {
        superSerial.println("Raise and shine");
        transition_to(MAX_USER_LIGHT, FREE_TRANSITION_SPEED);
      }
    }
    button_state = current_state;
  }
}

//////////////// LIGHT LEVEL HANDLER ////////////////////

struct
{
  uint32_t timestamp;
  linear_light_t power;
} g_light_shape[LIGHT_SHAPE_COUNT + 1];
int g_light_shape_count = 0;

linear_light_t g_light_goal = 0;
linear_light_t get_light_goal()
{
  return g_light_goal;
}

void transition_to(user_light_t next_light, uint32_t transition_ms)
{
  superSerial.printf("transition_to %d in %d ms\n", (int)next_light, (int)transition_ms);
  light_datapoint_t point = {.offset = transition_ms, .power = next_light};
  transition_with_shape(&point, 1);
}

void transition_with_shape(light_datapoint_t light_shape[], int light_shape_count)
{
  uint32_t start = millis();
  g_light_shape[0].timestamp = start;
  g_light_shape[0].power = get_light_level();

  int i;
  for (i = 0; i < light_shape_count; i++)
  {
    g_light_shape[i + 1].timestamp = start + light_shape[i].offset;

    g_light_shape[i + 1].power = userToLin(light_shape[i].power);
  }
  g_light_shape_count = light_shape_count;

  for (int i = 0; i <= light_shape_count; i++)
  {
    superSerial.printf("shape %i at %" PRIu32 " = %" PRIu16 "\n", i, g_light_shape[i].timestamp, g_light_shape[i].power);
  }
}

void update_light()
{
  uint32_t current = millis();
  for (int i = 0; i < g_light_shape_count; i++)
  {
    if (is_millis_increasing(g_light_shape[i].timestamp, current) && is_millis_increasing(current, g_light_shape[i + 1].timestamp))
    {
      uint64_t transition_start_time = g_light_shape[i].timestamp;
      uint64_t transition_end_time = g_light_shape[i + 1].timestamp;
      uint64_t transition_start_level = g_light_shape[i].power;
      uint64_t transition_end_level = g_light_shape[i + 1].power;
      g_light_goal = transition_end_level;
      uint64_t elapsed = current - transition_start_time;
      uint64_t total = transition_end_time - transition_start_time;
      uint64_t light = (transition_end_level * elapsed + transition_start_level * (total - elapsed)) / total;
      set_light_level(light);
      return;
    }
  }

  if (g_light_shape_count)
  {
    g_light_goal = g_light_shape[g_light_shape_count].power;
    set_light_level(g_light_shape[g_light_shape_count].power);
    superSerial.println(F("finished"));
    transition_with_shape(NULL, 0);
    g_light_shape_count = 0;
  }
}

bool is_in_transition()
{
  return g_light_shape_count;
}

bool is_millis_increasing(uint32_t before, uint32_t after)
{
  return after - before < 0x80000000;
}

linear_light_t g_current_light = 0;

linear_light_t get_light_level()
{
  return g_current_light;
}

gamma_light_t gamma_law(linear_light_t light)
{
  return std::pow(light / static_cast<float>(LINEAR_RANGE), GAMMA_COEFF) * TOTAL_GAMMA_RANGE;
}

void set_light_level(linear_light_t light)
{
  if (light == g_current_light)
    return;
  gamma_light_t gamma = gamma_law(light);
  if (gamma < HOT_RANGE)
  {
    analogWrite(HOT_LED, gamma / HOT_COEFF);
    analogWrite(COLD_LED, 0);
  }
  else if (gamma < TOTAL_GAMMA_RANGE)
  {
    analogWrite(HOT_LED, PWM_RANGE);
    analogWrite(COLD_LED, (gamma - HOT_RANGE) / COLD_COEFF);
  }
  else
  {
    analogWrite(HOT_LED, PWM_RANGE);
    analogWrite(COLD_LED, PWM_RANGE);
  }
  g_current_light = light;
}

/////////////////////////// Print //////////////////////////////////

template <size_t BUFFER_SIZE>
size_t SuperSerial<BUFFER_SIZE>::write(const uint8_t *buffer, size_t size)
{
  Serial.write(buffer, size);
  char *end = buffer_print + sizeof(buffer_print);
  size_t to_copy = std::min(size, static_cast<size_t>(end - buffer_ptr));
  memcpy(buffer_ptr, buffer, to_copy);
  buffer_ptr += to_copy;
  return to_copy;
}

template <size_t BUFFER_SIZE>
size_t SuperSerial<BUFFER_SIZE>::write(uint8_t byte)
{
  Serial.write(byte);
  return write(&byte, 1);
}

template <size_t BUFFER_SIZE>
void SuperSerial<BUFFER_SIZE>::send()
{
  server.send(200, "application/octet-stream", buffer_print, buffer_ptr - buffer_print);
  buffer_ptr = buffer_print;
}
