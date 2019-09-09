// Please read VController_v3.ino for information about the license and authors

// This page has the following parts:
// Section 1: KTN Initialization
// Section 2: KTN common MIDI in functions
// Section 3: KTN common MIDI out functions
// Section 4: KTN program change
// Section 5: KTN parameter control
// Section 6: KTN expression pedal control

// ********************************* Section 1: KTN Initialization ********************************************

// Boss KATANA settings:
#define KTN_MIDI_CHANNEL 1
#ifdef IS_VCMINI
#define KTN_PATCH_MIN 0
#define KTN_BANK_SIZE 3
#else // On the full VController we keep PANEL out of the patch bank select system
#define KTN_PATCH_MIN 1
#define KTN_BANK_SIZE 8
#endif
#define KTN_PATCH_MAX 88

// Messages are abbreviated to just the address and the data bytes. Checksum is calculated automatically
// Example: {0xF0, 0x41, 0x10, 0x00, 0x00, 0x00, 0x33, 0x12, 0x7F, 0x00, 0x00, 0x01, 0x01, 0x7F, 0xF7} is reduced to 0x7F000001, 0x01

#define KTN_EDITOR_MODE_ON 0x7F000001, 0x01
#define KTN_EDITOR_MODE_OFF 0x7F000001, 0x00
#define KTN_SELECT_PATCH_ADDRESS 0x00010000
#define KTN_CURRENT_PATCH_NAME_ADDRESS 0x60000000
#define KTN_CURRENT_PATCH_NUMBER_ADDRESS 0x00010000
#define KTN_MOD_TYPE_ADDRESS 0x60000140
#define KTN_FX_TYPE_ADDRESS 0x6000034C

#define KTN_TIME_DELAY1 0x60000562  // Accepts values from 00 00 (0 ms) to 0F 50 (2000 ms)
#define KTN_TIME_DELAY2 0x60001050  // Accepts values from 00 00 (0 ms) to 0F 50 (2000 ms)

#define KTN_READ_MIDI_TIME 1000

// Called at startup of VController
void MD_KTN_class::init() { // Default values for variables

  // Boss KATANA variables:
  enabled = DEVICE_DETECT; // Default value
  strcpy(device_name, "KTNA");
  strcpy(full_device_name, "Boss KATANA");
  current_patch_name.reserve(17);
  current_patch_name = "                ";
  patch_min = KTN_PATCH_MIN;
  patch_max = KTN_PATCH_MAX;
  //bank_size = 8;
  max_times_no_response = MAX_TIMES_NO_RESPONSE; // The number of times the KATANA does not have to respond before disconnection
  //  COSM_onoff = 0;
  //  nrml_pu_onoff = 0;
  sysex_delay_length = 5; // time between sysex messages (in msec).
  my_LED_colour = 2; // Default value: red
  MIDI_channel = KTN_MIDI_CHANNEL; // Default value
  //bank_number = 0; // Default value
  is_always_on = true; // Default value
  my_device_page1 = KTN_DEFAULT_PAGE1; // Default value
  my_device_page2 = KTN_DEFAULT_PAGE2; // Default value
  my_device_page3 = KTN_DEFAULT_PAGE3; // Default value
  my_device_page4 = KTN_DEFAULT_PAGE4; // Default value
  count_parameter_categories();
  midi_timer = 0;
}

void MD_KTN_class::update() {
  if (!connected) return;
  if (midi_timer > 0) { // Check timer is running
    if (millis() > midi_timer) {
      DEBUGMSG("M13 Midi timer expired!");
      request_patch_message(current_midi_message);
    }
  }
}

// ********************************* Section 2: KTN common MIDI in functions ********************************************

void MD_KTN_class::check_SYSEX_in(const unsigned char* sxdata, short unsigned int sxlength, uint8_t port) { // Check incoming sysex messages from  Called from MIDI:OnSysEx/OnSerialSysEx

  // Check if it is a message from a KATANA
  if ((port == MIDI_port) && (sxdata[1] == 0x41) && (sxdata[2] == MIDI_device_id) && (sxdata[3] == 0x00) && (sxdata[4] == 0x00) && (sxdata[5] == 0x00) && (sxdata[6] == 0x33) && (sxdata[7] == 0x12)) {
    uint32_t address = (sxdata[8] << 24) + (sxdata[9] << 16) + (sxdata[10] << 8) + sxdata[11]; // Make the address 32 bit

    // Check checksum
    uint16_t sum = 0;
    for (uint8_t i = 8; i < sxlength - 2; i++) sum += sxdata[i];
    bool checksum_ok = (sxdata[sxlength - 2] == calc_Roland_checksum(sum));

    if ((address == KTN_MOD_TYPE_ADDRESS) && (checksum_ok)) {
      mod_enabled = sxdata[12];
      current_mod_type = sxdata[13];
    }

    if ((address == KTN_FX_TYPE_ADDRESS) && (checksum_ok)) {
      fx_enabled = sxdata[12];
      current_fx_type = sxdata[13];
      if (last_requested_sysex_address != KTN_FX_TYPE_ADDRESS) update_page = REFRESH_PAGE; // Breaking a circle here
    }

    // Check if it is the current parameter
    if (address == last_requested_sysex_address) {
      switch (last_requested_sysex_type) {
        case REQUEST_PATCH_NAME:
          if (checksum_ok) {
            for (uint8_t count = 0; count < 16; count++) {
              SP[last_requested_sysex_switch].Label[count] = static_cast<char>(sxdata[count + 12]); // Add ascii character to the SP.Label String
            }

            if (SP[last_requested_sysex_switch].PP_number == patch_number) {
              current_patch_name = SP[last_requested_sysex_switch].Label; // Load patchname when it is read
              update_main_lcd = true; // And show it on the main LCD
            }
            DEBUGMSG(SP[last_requested_sysex_switch].Label);
            PAGE_request_next_switch();
          }
          else {
            PAGE_request_current_switch();
          }
          break;

        case REQUEST_PARAMETER_TYPE:
          if (checksum_ok) read_parameter(last_requested_sysex_switch, sxdata[12], sxdata[13]);
          PAGE_request_next_switch();
          break;
      }
    }

    // Check if it is the patch name (address: 0x60, 0x00, 0x00, 0x00)
    if ((address == KTN_CURRENT_PATCH_NAME_ADDRESS) && (checksum_ok)) {
      current_patch_name = "";
      for (uint8_t count = 12; count < 28; count++) {
        current_patch_name += static_cast<char>(sxdata[count]); //Add ascii character to Patch Name String
      }
      update_main_lcd = true;
    }

    if ((address == KTN_CURRENT_PATCH_NUMBER_ADDRESS) && (checksum_ok)) { // check if we are reading the current patch number
      patch_number = sxdata[13];
      do_after_patch_selection();
      update_page = REFRESH_PAGE;
    }

    // Check if we are reading the current patch for saving
    if (current_midi_message > 0) {
      read_patch_message(current_midi_message, sxdata, sxlength, checksum_ok);
    }
    else {
      // Midi forwarding to allow editing via VController
      if ((MIDI_port != USBMIDI_PORT) && (connected)) { // Forward data from Katana to editor
        MIDI_send_sysex(sxdata, sxlength, USBMIDI_PORT);
      }
    }
  }
}

void MD_KTN_class::forward_MIDI_message(const unsigned char* sxdata, short unsigned int sxlength) { // Forward data from editor to Katana
  if ((connected) && (sxdata[1] == 0x41) && (sxdata[2] == MIDI_device_id) && (sxdata[3] == 0x00) && (sxdata[4] == 0x00) && (sxdata[5] == 0x00) && (sxdata[6] == 0x33)) {
    check_sysex_delay();
    MIDI_send_sysex(sxdata, sxlength, MIDI_port);

    // Pull out the patch number
    if ((sxdata[7] == 0x12) && (sxdata[8] == 0x10) && (sxdata[10] == 0x00) && (sxdata[11] == 0x00)) {
      DEBUGMSG("HERE " + String(sxdata[9]));
      if (sxdata[9] < 8) {
        patch_number = sxdata[9];
        do_after_patch_selection();
        update_page = REFRESH_PAGE;
      }
    }
  }
}

void MD_KTN_class::check_PC_in(uint8_t program, uint8_t channel, uint8_t port) {  // Check incoming PC messages from  Called from MIDI:OnProgramChange

  // Check the source by checking the channel
  if ((port == MIDI_port) && (channel == MIDI_channel)) { // KTN sends a program change
    uint8_t new_patch_number = 0;
    if (program == 4) new_patch_number = 0; // Panel
    else if (program < 4) new_patch_number = program + 1; // CH1 - CH4
    else new_patch_number = program; // CH5 - CH8
    if (patch_number != new_patch_number) {
      patch_number = new_patch_number;
      request_current_patch_name();
      do_after_patch_selection();
      update_page = REFRESH_PAGE;
    }
  }
}

void MD_KTN_class::forward_PC_message(uint8_t program, uint8_t channel) { // Forward PC messages from editor to Katana
  if ((connected)  && (channel == MIDI_channel)) { // Editor sends a program change to the Katanan
    MIDI_send_PC(program, MIDI_channel, MIDI_port); // Forward the message to the Katana

    // Update the patch number on the VController as well
    uint8_t new_patch_number = 0;
    if (program == 4) new_patch_number = 0; // Panel
    else if (program < 4) new_patch_number = program + 1; // CH1 - CH4
    else new_patch_number = program; // CH5 - CH8
    if (patch_number != new_patch_number) {
      patch_number = new_patch_number;
      //request_current_patch_name();
      //do_after_patch_selection();
      if (!PAGE_check_on_page(my_device_number, patch_number)) { // Check if patch is on the page
        update_page = REFRESH_PAGE;
      }
      else {
        update_page = REFRESH_FX_ONLY;
      }
    }
  }
}


// Detection of KATANA

void MD_KTN_class::identity_check(const unsigned char* sxdata, short unsigned int sxlength, uint8_t port) {
  // Check if it is a KATANA
  if ((sxdata[5] == 0x41) && (sxdata[6] == 0x33) && (sxdata[7] == 0x03)) {
    no_response_counter = 0;
    if (connected == false) connect(sxdata[2], port); //Byte 2 contains the correct device ID

    // Midi forwarding to allow editing via VController
    if ((MIDI_port != USBMIDI_PORT) && (connected)) { // Forward data from Katana to editor
      MIDI_send_sysex(sxdata, sxlength, USBMIDI_PORT);
    }
  }
}

void MD_KTN_class::do_after_connect() {
  write_sysex(KTN_EDITOR_MODE_ON); // Put the KTN in EDITOR mode - otherwise patch number request will not work
  request_sysex(KTN_CURRENT_PATCH_NUMBER_ADDRESS, 2);
  request_current_patch_name();
  write_sysex(KTN_EDITOR_MODE_OFF); // Switch EDITOR mode off - to avoid unwanted feedback
  do_after_patch_selection();
  current_exp_pedal = 1;
  update_page = REFRESH_PAGE;
}


// ********************************* Section 3: KTN common MIDI out functions ********************************************

void MD_KTN_class::write_sysex(uint32_t address, uint8_t value) { // For sending one data byte

  uint8_t *ad = (uint8_t*)&address; //Split the 32-bit address into four bytes: ad[3], ad[2], ad[1] and ad[0]
  uint8_t checksum = calc_Roland_checksum(ad[3] + ad[2] + ad[1] + ad[0] + value); // Calculate the Roland checksum
  uint8_t sysexmessage[15] = {0xF0, 0x41, MIDI_device_id, 0x00, 0x00, 0x00, 0x33, 0x12, ad[3], ad[2], ad[1], ad[0], value, checksum, 0xF7};
  check_sysex_delay();
  MIDI_send_sysex(sysexmessage, 15, MIDI_port, 1);
}

void MD_KTN_class::write_sysex(uint32_t address, uint8_t value1, uint8_t value2) { // For sending two data bytes

  uint8_t *ad = (uint8_t*)&address; //Split the 32-bit address into four bytes: ad[3], ad[2], ad[1] and ad[0]
  uint8_t checksum = calc_Roland_checksum(ad[3] + ad[2] + ad[1] + ad[0] + value1 + value2); // Calculate the Roland checksum
  uint8_t sysexmessage[16] = {0xF0, 0x41, MIDI_device_id, 0x00, 0x00, 0x00, 0x33, 0x12, ad[3], ad[2], ad[1], ad[0], value1, value2, checksum, 0xF7};
  check_sysex_delay();
  MIDI_send_sysex(sysexmessage, 16, MIDI_port, 1);
}

void MD_KTN_class::request_sysex(uint32_t address, uint8_t no_of_bytes) {
  uint8_t *ad = (uint8_t*)&address; //Split the 32-bit address into four bytes: ad[3], ad[2], ad[1] and ad[0]
  uint8_t no1 = no_of_bytes >> 7;
  uint8_t no2 = no_of_bytes & 0x7F;
  uint8_t checksum = calc_Roland_checksum(ad[3] + ad[2] + ad[1] + ad[0] +  no1 + no2); // Calculate the Roland checksum
  uint8_t sysexmessage[18] = {0xF0, 0x41, MIDI_device_id, 0x00, 0x00, 0x00, 0x33, 0x11, ad[3], ad[2], ad[1], ad[0], 0x00, 0x00, no1, no2, checksum, 0xF7};
  check_sysex_delay();
  MIDI_send_sysex(sysexmessage, 18, MIDI_port, 1);
}

void MD_KTN_class::write_patch_data(uint32_t address, uint8_t index, uint8_t len) {
  uint8_t *ad = (uint8_t*)&address; //Split the 32-bit address into four bytes: ad[3], ad[2], ad[1] and ad[0]
  uint16_t checksum = ad[3] + ad[2] + ad[1] + ad[0];
  uint8_t sysexmessage[35] = {0xF0, 0x41, MIDI_device_id, 0x00, 0x00, 0x00, 0x33, 0x12, ad[3], ad[2], ad[1], ad[0]}; // Longest message is 34 bytes
  for (uint8_t i = 0; i < len; i++) {
    sysexmessage[i + 12] = KTN_patch_buffer[index + i];
    checksum += KTN_patch_buffer[index + i];
  }
  sysexmessage[len + 12] = calc_Roland_checksum(checksum);
  sysexmessage[len + 13] = 0xF7;
  check_sysex_delay();
  MIDI_send_sysex(sysexmessage, len + 14, MIDI_port, 1);
}

void MD_KTN_class::set_bpm() {
  if (connected) {
    uint16_t time1 = 60000 / Setting.Bpm;
    write_sysex(KTN_TIME_DELAY1, time1 >> 7, time1 & 0x7F);
    uint16_t time2 = 45000 / Setting.Bpm; // Delay 2 is always dotted eights!!!
    write_sysex(KTN_TIME_DELAY2, time2 >> 7, time2 & 0x7F);
    //DEBUGMSG("Katana: set delay 1 to " + String(time1) + " ms and delay 2 to " + String(time2) + " ms");
  }
}

// ********************************* Section 4: KTN program change ********************************************

void MD_KTN_class::select_patch(uint16_t new_patch) {
  uint8_t send_number;
  if (new_patch == patch_number) unmute();
  patch_number = new_patch;
  if (new_patch < 9) {
    // Order of patches is not logical, so we have to fix it
    if (new_patch == 0) send_number = 4; // PC 0 will select PANEL
    else if (new_patch <= 4) send_number = new_patch - 1; // PC 1 - 4 will select CH1 - CH4
    else send_number = new_patch; // PC 5 - 8 will select CH5 - CH8
    if ((send_number == prev_patch_number) || (prev_patch_number == 255)) {
      // Can't reselect patch via midi Pc, so we send a different PC number first...
      if (prev_patch_number != 4) MIDI_send_PC(4, MIDI_channel, MIDI_port);
      else MIDI_send_PC(3, MIDI_channel, MIDI_port);
    }
    MIDI_send_PC(send_number, MIDI_channel, MIDI_port);
    prev_patch_number = send_number;
    DEBUGMSG("out(" + String(device_name) + ") PC" + String(new_patch)); //Debug
  }
  else { // Load patch from EEPROM
    load_patch(new_patch - 9);
    DEBUGMSG("Loading patch " + String(new_patch)); //Debug
  }
  do_after_patch_selection();
}

void MD_KTN_class::do_after_patch_selection() {
  request_onoff = false;
  is_on = connected;
  if (Setting.Send_global_tempo_after_patch_change == true) {
    delay(5); // KTN misses send bpm command...
    set_bpm();
  }
  Current_patch_number = patch_number;
  update_LEDS = true;
  update_main_lcd = true;
  request_sysex(KTN_MOD_TYPE_ADDRESS, 2);
  request_sysex(KTN_FX_TYPE_ADDRESS, 2);
  //EEPROM_write_patch_number(KTN, patch_number);
  if (!PAGE_check_on_page(my_device_number, patch_number)) { // Check if patch is on the page
    update_page = REFRESH_PAGE;
  }
  else {
    update_page = REFRESH_FX_ONLY;
  }
}

bool MD_KTN_class::request_patch_name(uint8_t sw, uint16_t number) {
  if (number == 0) {
    String patch_name = "PANEL";
    LCD_set_SP_label(sw, patch_name);
    return true;;
  }
  if (number < 9) {
    //number++;
    uint32_t Address = 0x10000000 + (number * 0x10000); //Calculate the address where the patchname is stored on the KATANA
    last_requested_sysex_address = Address;
    last_requested_sysex_type = REQUEST_PATCH_NAME;
    last_requested_sysex_switch = sw;
    request_sysex(Address, 16); //Request the 16 bytes of the KTN patchname
    return false;
  }
  else if (number < 89) {
    String patch_name = "";
    EEPROM_read_KTN_title(number - 9, patch_name);
    LCD_set_SP_label(sw, patch_name);
    return true;
  }
  else { // We cannot read this name
    LCD_clear_SP_label(sw);
    return true;
  }
}

void MD_KTN_class::request_current_patch_name() {
  request_sysex(KTN_CURRENT_PATCH_NAME_ADDRESS, 16);
}

void MD_KTN_class::number_format(uint16_t number, String &Output) {
  if (number == 0) Output += "PNL";
  else if (number < 9) Output += "CH" + String(number);
  else Output += "VC" + String((number - 9) / KTN_BANK_SIZE) + "." + String((number - 9) % KTN_BANK_SIZE + 1);
}

void MD_KTN_class::direct_select_format(uint16_t number, String &Output) {
  if (direct_select_state == 0) {
    if ((bank_select_number == 0) && (number == 0)) Output += "CH_";
    else Output += "VC" + String(bank_select_number * 10 + number - 1) + "._";
  }
  else {
    if (bank_select_number == 0) {
      if (number == 0) Output += "PANEL";
      else Output += "CH" + String(number);
    }
    else Output += "VC" + String(bank_select_number - 1) + "." + String(number);
  }
}

bool MD_KTN_class::valid_direct_select_switch(uint8_t number) {
  bool result = false;
  if (direct_select_state == 0) { // Show all switches on first digit
    result = ((number * KTN_BANK_SIZE) + (bank_select_number * KTN_BANK_SIZE * 10) <= (patch_max - patch_min));
  }
  else {
    if (bank_select_number == 0) { // Show switches 1 - 8 + PANEL on second digit
      if (number < 9) result = true;
    }
    else if ((number > 0) && (number < 9)) result = true;
  }
  return result;
}

void MD_KTN_class::direct_select_start() {
  Previous_bank_size = bank_size; // Remember the bank size
  device_in_bank_selection = my_device_number + 1;
  bank_size = 80;
  bank_select_number = 0; // Reset bank number
  direct_select_state = 0;
}

uint16_t MD_KTN_class::direct_select_patch_number_to_request(uint8_t number) {
  if (direct_select_state == 0) return (number * 8) + (bank_select_number * 80) + patch_min;
  else {
    uint16_t new_patch_number = (bank_select_number * 8) + number + patch_min;
    if (new_patch_number <= 0) new_patch_number = 1;
    return new_patch_number - 1;
  }
}

void MD_KTN_class::direct_select_press(uint8_t number) {
  if (!valid_direct_select_switch(number)) return;
  if (direct_select_state == 0) {
    // First digit pressed
    bank_select_number = number + (bank_select_number * 10);
    bank_size = KTN_BANK_SIZE;
    direct_select_state = 1;
  }
  else {
    // Second digit pressed
    device_in_bank_selection = 0;
    uint16_t new_patch = (bank_select_number * KTN_BANK_SIZE) + number + patch_min;
    if (new_patch <= 0) new_patch = 1;
    if (new_patch > patch_max) new_patch = patch_max;
    //bank_number = ((new_patch - patch_min - 1) / Previous_bank_size); // Set bank number to the new patch
    bank_size = Previous_bank_size;
    //bank_select_number = bank_number;
    select_patch(new_patch - 1);
    update_bank_number(patch_number);
    SCO_select_page(Previous_page);
  }
}

// ** Patch storage in EEPROM memory
// As memory is limited, we will only save and restore the parameters of the effects that are selected
// In the EEPROM memory we reserved 192 bytes for each patch. The total size has to stay below that number
// All active parameters are stored, apart from the user scales of the harmonist effect

// Patch memory structure on VController
#define KTN_PATCH_NAME_INDEX   0  // Patch name (16 parameters)
#define KTN_BOOST_INDEX       16  // Boost FX (15 parameters)
#define KTN_AMP_INDEX         31  // Amp index (9 parameters)
#define KTN_EQ_INDEX          40  // Eq (12 parameters)
#define KTN_MOD_BASE_INDEX    52  // Mod base (2 parameters: ON/OFF and type)
#define KTN_MOD_INDEX         54  // MOD effect settings for active effect (max 15 parameters)
#define KTN_FX_BASE_INDEX     69  // FX base (2 parameters: ON/OFF and type)
#define KTN_FX_INDEX          71  // FX effect settings for active effect (max 15 parameters)
#define KTN_DELAY1_INDEX      86  // Delay 1 (18 parameters)
#define KTN_DELAY1_SDE_INDEX 104  // Delay 1 SDE-3000 settings (5)
#define KTN_DELAY2_INDEX     109  // Delay 2 (18 parameters)
#define KTN_DELAY2_SDE_INDEX 127  // Delay 2 SDE-3000 settings (5)// 074 - 084 Reverb (11 parameters)
#define KTN_REVERB_INDEX     132  // Reverb (11 parameters)
#define KTN_FOOT_VOL_INDEX   143  // Foot volume (4 parameters)
#define KTN_S_R_LOOP_INDEX   147  // S/R loop (4 parameters)
#define KTN_NOISE_GATE_INDEX 151  // Noise gate (4 parameters)
#define KTN_MASTER_KEY_INDEX 155  // Master key
#define KTN_FX_CHAIN_INDEX   156  // FX chain (20 parameters)
#define KTN_GEQ_BASE_INDEX   176  // GEQ (12 parameters)

// 188 Total memory size

struct KTN_patch_memory_struct { // Address map for the essential patch data that will be stored
  uint16_t Address; // The start address of the data on the Katana
  uint8_t Length;   // The Length of the data
  uint8_t Index;    // The index in the KTN_patch_buffer[] array
};

const PROGMEM KTN_patch_memory_struct KTN_patch_memory[] = {
  { 0x0720, 20, KTN_FX_CHAIN_INDEX   },  // FX chain
  { 0x0051, 9, KTN_AMP_INDEX         },  // Amp
  { 0x0030, 15, KTN_BOOST_INDEX      },  // Boost effect
  { 0x0130, 12, KTN_EQ_INDEX         },  // Eq
  { 0x0140, 2, KTN_MOD_BASE_INDEX    },  // Mod base
  { 0x034C, 2, KTN_FX_BASE_INDEX     },  // FX base
  { 0x0630, 4, KTN_FOOT_VOL_INDEX    },  // Foot volume
  { 0x0655, 4, KTN_S_R_LOOP_INDEX    },  // S/R loop
  { 0x0560, 18, KTN_DELAY1_INDEX     },  // Delay 1
  { 0x0610, 11, KTN_REVERB_INDEX     },  // Reverb
  { 0x104E, 18, KTN_DELAY2_INDEX     },  // Delay 2
  { 0x0663, 4, KTN_NOISE_GATE_INDEX  },  // Noise gate
  { 0x0718, 1, KTN_MASTER_KEY_INDEX  },  // Master key
  { 0x1049, 5, KTN_DELAY1_SDE_INDEX  },  // Delay 1 - SDE3000 settings
  { 0x1049, 5, KTN_DELAY2_SDE_INDEX  },  // Delay 2 - SDE3000 settings
  { 0x1104, 12, KTN_GEQ_BASE_INDEX   },  // GEQ
  { 0x0000, 16, KTN_PATCH_NAME_INDEX },  // Patch name (1)
};

const uint8_t KTN_NUMBER_OF_PATCH_MESSAGES = sizeof(KTN_patch_memory) / sizeof(KTN_patch_memory[0]);

#define KTN_NUMBER_OF_FX_TYPES 39

struct KTN_fx_memory_struct { // Address map for the essential patch data that will be stored
  uint16_t Mod_address; // The start address of the mod parameters on the Katana
  uint16_t FX_address;  // The start address of the fx parameters on the Katana
  uint8_t Length;       // The Length of the data
};

const PROGMEM KTN_fx_memory_struct KTN_fx_memory[KTN_NUMBER_OF_FX_TYPES] = {
  { 0x014C, 0x0358, 7 },  // TOUCHWAH
  { 0x0154, 0x0360, 7 },  // AUTO WAH
  { 0x015C, 0x0368, 6 },  // SUB WAH
  { 0x0163, 0x036F, 5 },  // COMPRSOR
  { 0x0169, 0x0375, 6 },  // LIMITER
  { 0x0142, 0x034E, 8 },  // DIST
  { 0x0170, 0x037C, 11 }, // GRAPH EQ
  { 0x017C, 0x0408, 11 }, // PARAM EQ
  { 0x0208, 0x0414, 5 },  // TONE MOD
  { 0x020E, 0x041A, 5 },  // GTR SIM",
  { 0x0214, 0x0420, 3 },  // SLOW GR
  { 0x0218, 0x0424, 7 },  // DEFRET
  { 0x0220, 0x042C, 8 },  // WAV SNTH
  { 0x0229, 0x0435, 7 },  // SITARSIM
  { 0x0231, 0x043D, 3 },  // OCTAVE
  { 0x0235, 0x0441, 15 }, // PITCH SH
  { 0x0245, 0x0451, 11 },  // HARMONST - length is 35 if we were to store user scales as well
  { 0x0269, 0x0475, 3 },  // SND HOLD
  { 0x026E, 0x0479, 7 },  // AC. PROC
  { 0x0275, 0x0501, 8 },  // PHASER",
  { 0x027E, 0x050A, 8 },  // FLANGER
  { 0x0307, 0x0513, 4 },  // TREMOLO
  { 0x030E, 0x051A, 5 },  // ROTARY 1
  { 0x0314, 0x0520, 3 },  // UNI-V
  { 0x0318, 0x0524, 6 },  // PAN
  { 0x031F, 0x052B, 5 },  // SLICER
  { 0x0325, 0x0531, 5 },  // VIBRATO
  { 0x032B, 0x0537, 4 },  // RING MOD
  { 0x0330, 0x053C, 8 },  // HUMANIZR
  { 0x0339, 0x0545, 9 },  // 2x2 CHOR
  { 0x0343, 0x054F, 7 },  // SUB DELY
  { 0x1010, 0x101F, 4 },  // AcGtrSim
  { 0x1016, 0x1025, 9 },  // ROTARY 2
  { 0x0000, 0x102F, 7 },  // TeraEcho
  { 0x0000, 0x1037, 5 },  // OVERTONE
  { 0x103D, 0x1043, 2 },  // PHAS 90E
  { 0x103F, 0x1045, 4 },  // FLGR117E
  { 0x1068, 0x1076, 5 },  // WAH95E
  { 0x106D, 0x107B, 9 },  // DC30
};

void MD_KTN_class::load_patch(uint8_t number) {

  // Load patch from EEPROM in KTN_patch_memory
  EEPROM_load_KTN_patch(number, KTN_patch_buffer, KTN_PATCH_SIZE);

  // Write the patch from KTN_patch_memory to the Katana
  current_mod_type = KTN_patch_buffer[KTN_MOD_BASE_INDEX + 1];
  if (KTN_fx_memory[current_mod_type].Mod_address > 0x0000) write_patch_data(0x60000000 + KTN_fx_memory[current_mod_type].Mod_address, KTN_MOD_INDEX, KTN_fx_memory[current_mod_type].Length);

  current_fx_type = KTN_patch_buffer[KTN_FX_BASE_INDEX + 1];
  if (KTN_fx_memory[current_fx_type].FX_address > 0x0000) write_patch_data(0x60000000 + KTN_fx_memory[current_fx_type].FX_address, KTN_FX_INDEX, KTN_fx_memory[current_fx_type].Length);

  if (FX_chain_changed()) write_patch_data(0x60000000 + KTN_patch_memory[0].Address, KTN_patch_memory[0].Index, KTN_patch_memory[0].Length); // Write FX chain only when it is different to avoid gap

  for (uint8_t i = 1; i < KTN_NUMBER_OF_PATCH_MESSAGES; i++) {
    write_patch_data(0x60000000 + KTN_patch_memory[i].Address, KTN_patch_memory[i].Index, KTN_patch_memory[i].Length);
  }

  save_patch_number = number; // Remember memory number for the next time we save a patch
}

bool MD_KTN_class::FX_chain_changed() {
  bool changed = false;
  for (uint8_t i = 0; i < KTN_FX_CHAIN_SIZE; i++) {
    if (KTN_FX_chain[i] != KTN_patch_buffer[i + KTN_FX_CHAIN_INDEX]) {
      changed = true;
      KTN_FX_chain[i] = KTN_patch_buffer[i + KTN_FX_CHAIN_INDEX]; // Update the KTN_FX_chain with the new data
    }
  }
  return changed;
}

void MD_KTN_class::save_patch() {

  // Request the data from the Katana
  MIDI_disable_device_check();
  request_patch_message(1);
}

void MD_KTN_class::request_patch_message(uint8_t number) {
  current_midi_message = number;
  midi_timer = millis() + KTN_READ_MIDI_TIME; // Set the timer
  if ((number > 0) && (number <= KTN_NUMBER_OF_PATCH_MESSAGES)) {
    current_midi_message_address = 0x60000000 + KTN_patch_memory[number - 1].Address;
    request_sysex(current_midi_message_address, KTN_patch_memory[number - 1].Length);
  }
  if (number == KTN_NUMBER_OF_PATCH_MESSAGES + 1) { // Selected MOD data
    if (KTN_fx_memory[current_mod_type].Mod_address > 0x0000) {
      current_midi_message_address = 0x60000000 + KTN_fx_memory[current_mod_type].Mod_address;
      request_sysex(current_midi_message_address, KTN_fx_memory[current_mod_type].Length);
    }
    else {
      request_patch_message(current_midi_message + 1); // Request the next midi message
    }
  }
  if (number == KTN_NUMBER_OF_PATCH_MESSAGES + 2) { // Selected FX data
    if (KTN_fx_memory[current_fx_type].FX_address > 0x0000) {
      current_midi_message_address = 0x60000000 + KTN_fx_memory[current_fx_type].FX_address;
      request_sysex(current_midi_message_address, KTN_fx_memory[current_fx_type].Length);
    }
    else {
      request_patch_message(current_midi_message + 1); // Request the next midi message
    }
  }
}

void MD_KTN_class::read_patch_message(uint8_t number, const unsigned char* sxdata, short unsigned int sxlength, bool checksum_ok) {
  uint32_t address = (sxdata[8] << 24) + (sxdata[9] << 16) + (sxdata[10] << 8) + sxdata[11]; // Make the address 32 bit
  if ((address == current_midi_message_address) && (number > 0)) {
    // Store data to KTN_patch_buffer[] array
    uint8_t start_index = 0;
    uint8_t data_length = 0;
    if (number <= KTN_NUMBER_OF_PATCH_MESSAGES) {
      start_index = KTN_patch_memory[number - 1].Index;
      data_length = KTN_patch_memory[number - 1].Length;
    }
    if (number == KTN_NUMBER_OF_PATCH_MESSAGES + 1) { // Selected Mod data
      start_index = KTN_MOD_INDEX;
      data_length = KTN_fx_memory[current_mod_type].Length;
    }
    if (number == KTN_NUMBER_OF_PATCH_MESSAGES + 2) { // Selected FX data
      start_index = KTN_FX_INDEX;
      data_length = KTN_fx_memory[current_fx_type].Length;
    }

    if (checksum_ok) {
      for (uint8_t i = 0; i < data_length; i++) {
        KTN_patch_buffer[start_index + i] = sxdata[i + 12];
      }
    }
    else { // Read error
      request_patch_message(current_midi_message);
      return;
    }

    if (address == 0x0140) { // Mod base - save the correct mod_type
      current_mod_type = sxdata[13];
    }

    if (address == 0x034C) { // FX base - save the correct fx_type
      current_fx_type = sxdata[13];
    }

    // Request next message
    if (number < KTN_NUMBER_OF_PATCH_MESSAGES + 2) {
      request_patch_message(current_midi_message + 1);
    }
    else {
      DEBUGMSG("Patch data read succesfully from Katana");
      MIDI_enable_device_check();
      midi_timer = 0;

      // Dump data (debug)
      MIDI_debug_sysex(KTN_patch_buffer, KTN_PATCH_SIZE, 255, true);

      open_menu_for_Katana_edit = true;
      SCO_select_page(PAGE_MENU); // Open the menu
      open_menu_for_Katana_edit = false;

    }
  }
}

void MD_KTN_class::store_patch() {
  // Store to EEPROM
  EEPROM_save_KTN_patch(save_patch_number, KTN_patch_buffer, KTN_PATCH_SIZE);
  LCD_show_popup_label("Patch saved.", MESSAGE_TIMER_LENGTH);
  select_patch(save_patch_number + 9); // Select the saved patch
}

bool MD_KTN_class::exchange_patch() {
  uint8_t old_save_number = patch_number - 9;
  if ((patch_number >= 9) && (old_save_number != save_patch_number)) {
    uint8_t temp_patch[KTN_PATCH_SIZE];
    EEPROM_load_KTN_patch(save_patch_number, temp_patch, KTN_PATCH_SIZE);
    EEPROM_save_KTN_patch(old_save_number, temp_patch, KTN_PATCH_SIZE);
    EEPROM_save_KTN_patch(save_patch_number, KTN_patch_buffer, KTN_PATCH_SIZE);
    LCD_show_popup_label("Patches swapped", MESSAGE_TIMER_LENGTH);
    select_patch(save_patch_number + 9); // Select the saved patch
    return true; // Swap succesfull
  }
  else {
    LCD_show_popup_label("Not possible", MESSAGE_TIMER_LENGTH);
    return false;
  }
}

void MD_KTN_class::read_patch_name_from_buffer(String &txt) { // Used for rename patch feature
  for (uint8_t i = 0; i < 16; i++) {
    txt += static_cast<char>(KTN_patch_buffer[i]);
  }
}

void MD_KTN_class::store_patch_name_to_buffer(String txt) { // Used for rename patch feature
  for (uint8_t i = 0; i < 16; i++) {
    KTN_patch_buffer[i] = txt[i];
  }
}

// Here is how I made this: When a patch is saved, the contents of the current patch memory are shown in the Serial Monitor when DEBUG_NORMAL is on.
// Copy it and paste it in a word document. Edit it by replacing the space with ", 0x", split it in chunks of 20 bytes and you have the default patch data!!!

const PROGMEM uint8_t KTN_default_patch[KTN_PATCH_SIZE] = {
  0x4e, 0x65, 0x77, 0x20, 0x50, 0x72, 0x65, 0x73, 0x65, 0x74, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x0c, 0x2e, 0x3c,
  0x32, 0x00, 0x32, 0x41, 0x00, 0x03, 0x34, 0x32, 0x32, 0x32, 0x32, 0x0b, 0x1e, 0x0a, 0x32, 0x32, 0x32, 0x32, 0x32, 0x00,
  0x00, 0x00, 0x14, 0x0e, 0x01, 0x14, 0x17, 0x01, 0x14, 0x14, 0x0e, 0x14, 0x00, 0x02, 0x00, 0x64, 0x00, 0x64, 0x64, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x07, 0x15, 0x4d, 0x4b, 0x54, 0x09, 0x3a, 0x16, 0x49,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x06, 0x20, 0x19, 0x0a, 0x2e, 0x64, 0x32, 0x00, 0x64, 0x16, 0x0a, 0x32,
  0x03, 0x10, 0x16, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x04, 0x58, 0x1e, 0x0e, 0x50, 0x63, 0x00, 0x07, 0x68,
  0x0a, 0x0e, 0x03, 0x07, 0x68, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x1d, 0x00, 0x0a, 0x0e, 0x08, 0x05,
  0x32, 0x64, 0x05, 0x02, 0x00, 0x64, 0x64, 0x00, 0x00, 0x32, 0x32, 0x00, 0x05, 0x32, 0x00, 0x00, 0x11, 0x0f, 0x05, 0x07,
  0x06, 0x02, 0x0d, 0x0c, 0x04, 0x01, 0x08, 0x09, 0x0e, 0x12, 0x00, 0x03, 0x0b, 0x0a, 0x13, 0x10, 0x00, 0x18, 0x18, 0x18,
  0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
};

void MD_KTN_class::load_patch_buffer_with_default_patch() {
  memcpy(KTN_patch_buffer, KTN_default_patch, KTN_PATCH_SIZE);
}

// ********************************* Section 5: KTN parameter control ********************************************

// Procedures for the KTN_PARAMETER:
// 1. Load in SP array L load_page()
// 2. Request parameter state - in PAGE_request_current_switch()
// 3. Read parameter state - KTN_read_parameter() below
// 4. Press switch - KTN_parameter_press() below - also calls check_update_label()
// 5. Release switch - KTN_parameter_release() below - also calls check_update_label()

// Parameter categories
#define KTN_CAT_NONE 0 // Some parameters cannot be read - should be in a category
#define KTN_CAT_BOOST 1
#define KTN_CAT_MOD 2
#define KTN_CAT_FX 3
#define KTN_CAT_AMP 4
#define KTN_CAT_DLY1 5
#define KTN_CAT_DLY2 6
#define KTN_CAT_RVB 7
#define KTN_CAT_EQ 8
#define KTN_CAT_MISC 9
#define KTN_CAT_GLOBAL 10
#define KTN_NUMBER_OF_FX_CATEGORIES 10

struct KTN_parameter_category_struct {
  char Name[17]; // The name for the label
};

const PROGMEM KTN_parameter_category_struct KTN_parameter_category[] = {
  { "BOOST" },
  { "MOD" },
  { "FX" },
  { "AMP" },
  { "DELAY1" },
  { "DELAY2" },
  { "REVERB" },
  { "EQ" },
  { "MISC" },
  { "GLOBAL" },
};

struct KTN_parameter_struct { // Combines all the data we need for controlling a parameter in a device
  //uint16_t Target; // Target of the assign as given in the assignments of the KTN / GR55
  uint16_t Address; // The address of the parameter
  uint8_t NumVals; // The number of values for this parameter
  char Name[17]; // The name for the label
  uint16_t Sublist; // Which sublist to read for the FX or amp type - 0 if second byte does not contain the type or if there is no sublist +100 Show value from sublist.
  uint8_t Colour; // The colour for this effect.
  uint8_t Category; // The category of this effect
};


#define KTN_FX_COLOUR 255 // Just a colour number to pick the colour from the KTN_FX_colours table
#define KTN_MOD_TYPE_COLOUR 254 //Another number for the FX type
#define KTN_FX_TYPE_COLOUR 253 //Another number for the FX type
#define KTN_FX_BUTTON_COLOUR 253

#define SUBLIST_FROM_BYTE2 32768 //Bit 16 set high means read sublist frm byte 2
#define SHOW_NUMBER 31767 // Special number for showing the number (used for gain/volume/etc) - set in sublist
#define SHOW_TONE_NUMBER 31766 // To show 00-64:-50-+50
#define SHOW_PAN 31765 // Special number for showing the pan- set in sublist
#define SHOW_DELAY_TIME 31764 // To show 1 - 2000 ms
#define SHOW_CUT_BOOST 31763 // To show -20 dB - + 20 dB
#define SHOW_RVB_TIME 31762
#define SHOW_PITCH_NUMBER 31761 // To show -24 - +24
#define SHOW_MILLIS 31760 // To show msec in steps of 0.5
#define SHOW_NUMBER_PLUS_ONE 31759 // Show number plus one

const PROGMEM KTN_parameter_struct KTN_parameters[] = {
  {0x0030, 2, "BOOST", 1 | SUBLIST_FROM_BYTE2, FX_DIST_TYPE, KTN_CAT_BOOST}, //00
  {0x0031, 22, "BST TP", 1, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0032, 121, "BST DRIVE", SHOW_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0033, 101, "BST BOTTOM", SHOW_TONE_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0034, 101, "BST TONE", SHOW_TONE_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0035, 2, "BST SOLO SW", 0, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0036, 101, "BST SOLO LVL", SHOW_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0037, 101, "BST FX LVL", SHOW_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0038, 101, "BST DIR LVL", SHOW_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0039, 8,   "BST CUST.TP", 273, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x003A, 101, "BST CUST.BTM", SHOW_TONE_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST}, // 10
  {0x003B, 101, "BST CUST.TOP", SHOW_TONE_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x003C, 101, "BST CUST.LOW", SHOW_TONE_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x003D, 101, "BST CUST.HI", SHOW_TONE_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x003E, 101, "BST CUST.CHAR", SHOW_TONE_NUMBER, FX_DIST_TYPE, KTN_CAT_BOOST},
  {0x0140, 2, "MOD", 23 | SUBLIST_FROM_BYTE2, KTN_FX_COLOUR, KTN_CAT_MOD},
  {0x0141, 39, "MOD TP", 23, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8000, 101, "MOD PAR 01", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8001, 101, "MOD PAR 02", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8002, 101, "MOD PAR 03", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8003, 101, "MOD PAR 04", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD}, // 20
  {0x8004, 101, "MOD PAR 05", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8005, 101, "MOD PAR 06", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8006, 101, "MOD PAR 07", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8007, 101, "MOD PAR 08", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8008, 101, "MOD PAR 09", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x8009, 101, "MOD PAR 10", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x800A, 101, "MOD PAR 11", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x800B, 101, "MOD PAR 12", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x800C, 101, "MOD PAR 13", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x800D, 101, "MOD PAR 14", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD}, // 30
  {0x800E, 101, "MOD PAR 15", SHOW_NUMBER, KTN_MOD_TYPE_COLOUR, KTN_CAT_MOD},
  {0x0051, 26, "AMP TP", 62, FX_AMP_TYPE, KTN_CAT_AMP},
  {0x0052, 121, "AMP GAIN", SHOW_NUMBER, FX_AMP_TYPE, KTN_CAT_AMP},
  {0x0054, 101, "AMP BASS", SHOW_NUMBER, FX_AMP_TYPE, KTN_CAT_AMP},
  {0x0055, 101, "AMP MIDDLE", SHOW_NUMBER, FX_AMP_TYPE, KTN_CAT_AMP},
  {0x0056, 101, "AMP TREBLE", SHOW_NUMBER, FX_AMP_TYPE, KTN_CAT_AMP},
  {0x0057, 101, "AMP PRESCENCE", SHOW_NUMBER, FX_AMP_TYPE, KTN_CAT_AMP},
  {0x0059, 2, "AMP BRIGHT", 0, FX_AMP_TYPE, KTN_CAT_AMP},
  {0x0058, 101, "AMP LEVEL", SHOW_NUMBER, FX_AMP_TYPE, KTN_CAT_AMP},
  {0x0130, 2, "EQ SW", 0, FX_FILTER_TYPE, KTN_CAT_EQ}, // 40
  {0x1104, 2, "EQ TYPE", 287, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0131, 18, "EQ LOW CUT", 111, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0132, 41, "EQ LOW LVL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0133, 26, "EQ L-M FRQ", 112, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0134, 6, "EQ L-M Q", 142, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0135, 41, "EQ L-M LVL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0136, 26, "EQ H-M FRQ", 112, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0137, 6, "EQ H-M Q", 142, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0138, 41, "EQ H-M LVL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0139, 15, "EQ HI CUT", 127, FX_FILTER_TYPE, KTN_CAT_EQ}, // 50
  {0x013A, 41, "EQ HI LVL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x013B, 101, "EQ LEVEL", SHOW_NUMBER, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x1105, 41, "GEQ 31 HZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x1106, 41, "GEQ 62 HZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x1107, 41, "GEQ 125 HZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x1108, 41, "GEQ 250 HZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x1109, 41, "GEQ 500 HZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x110A, 41, "GEQ 1 kHZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x110B, 41, "GEQ 2 kHZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x110C, 41, "GEQ 4 kHZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ}, // 60
  {0x110D, 41, "GEQ 8 kHZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x110E, 41, "GEQ 16 kHZ", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x110F, 41, "GEQ LEVEL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_EQ},
  {0x0560, 2, "DLY1", 90  | SUBLIST_FROM_BYTE2, FX_DELAY_TYPE, KTN_CAT_DLY1},
  {0x0561, 11, "DLY1 TP", 90, FX_DELAY_TYPE, KTN_CAT_DLY1},
  {0x0562, TIME_2000, "DLY1 TIME", SHOW_DELAY_TIME, FX_DELAY_TYPE, KTN_CAT_DLY1},
  {0x0564, 101, "DLY1 F.BACK", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY1},
  {0x0565, 15, "DLY1 H.CUT", 127, FX_DELAY_TYPE, KTN_CAT_DLY1},
  {0x0566, 101, "DLY1 FX LVL", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY1},
  {0x0567, 101, "DLY1 DIR LVL", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY1}, // 70
  {0x0573, 101, "DLY1 MOD RATE", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY1},
  {0x0574, 101, "DLY1 MOD DPTH", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY1},
  {0x034C, 2, "FX", 23  | SUBLIST_FROM_BYTE2, KTN_FX_COLOUR, KTN_CAT_FX},
  {0x034D, 39, "FX TYPE", 23, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9000, 101, "FX PAR 01", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9001, 101, "FX PAR 02", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9002, 101, "FX PAR 03", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9003, 101, "FX PAR 04", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9004, 101, "FX PAR 05", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9005, 101, "FX PAR 06", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX}, // 80
  {0x9006, 101, "FX PAR 07", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9007, 101, "FX PAR 08", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9008, 101, "FX PAR 09", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x9009, 101, "FX PAR 10", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x900A, 101, "FX PAR 11", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x900B, 101, "FX PAR 12", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x900C, 101, "FX PAR 13", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x900D, 101, "FX PAR 14", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x900E, 101, "FX PAR 15", SHOW_NUMBER, KTN_FX_TYPE_COLOUR, KTN_CAT_FX},
  {0x104E, 2, "DLY2", 90  | SUBLIST_FROM_BYTE2, FX_DELAY_TYPE, KTN_CAT_DLY2}, // 90
  {0x104F, 11, "DLY2 TP", 90, FX_DELAY_TYPE, KTN_CAT_DLY2},
  {0x1050, TIME_2000, "DLY2 TIME", SHOW_DELAY_TIME, FX_DELAY_TYPE, KTN_CAT_DLY2},
  {0x1052, 101, "DLY2 F.BACK", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY2},
  {0x1053, 15, "DLY2 H.CUT", 127, FX_DELAY_TYPE, KTN_CAT_DLY2},
  {0x1054, 101, "DLY2 FX LVL", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY2},
  {0x1055, 101, "DLY2 DIR LVL", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY2},
  {0x1061, 101, "DLY2 MOD RATE", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY2},
  {0x1062, 101, "DLY2 MOD DPTH", SHOW_NUMBER, FX_DELAY_TYPE, KTN_CAT_DLY2},
  {0x0610, 2, "RVB SW", 101  | SUBLIST_FROM_BYTE2, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x0611, 7, "RVB TP", 101, FX_REVERB_TYPE, KTN_CAT_RVB},                     // 100
  {0x0612, 100, "RVB TIME", SHOW_RVB_TIME, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x0613, TIME_500, "RVB PRE", SHOW_DELAY_TIME, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x0615, 18, "RVB L.CUT", 111, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x0616, 15, "RVB H.CUT", 127, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x0617, 11, "RVB DENS", SHOW_NUMBER, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x0618, 101, "FX LVL", SHOW_NUMBER, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x0619, 101, "DIR LVL", SHOW_NUMBER, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x061A, 101, "SPRING SENS", SHOW_NUMBER, FX_REVERB_TYPE, KTN_CAT_RVB},
  {0x0655, 2, "S/R LOOP", 0, FX_FILTER_TYPE, KTN_CAT_MISC},
  {0x0656, 2, "S/R MODE", 148, FX_FILTER_TYPE, KTN_CAT_MISC},               // 110
  {0x0657, 101, "S/R SEND LVL", SHOW_NUMBER, FX_FILTER_TYPE, KTN_CAT_MISC},
  {0x0658, 101, "S/R RET. LVL", SHOW_NUMBER, FX_FILTER_TYPE, KTN_CAT_MISC},
  {0x0663, 2, "N/S SW", 0, FX_DYNAMICS_TYPE, KTN_CAT_MISC},
  {0x0664, 101, "N/S THRESH", SHOW_NUMBER, FX_DYNAMICS_TYPE, KTN_CAT_MISC},
  {0x0665, 101, "N/S RLEASE", SHOW_NUMBER, FX_DYNAMICS_TYPE, KTN_CAT_MISC},
  {0x0633, 101, "FOOT VOL", SHOW_NUMBER, FX_FILTER_TYPE, KTN_CAT_MISC},
  {0x1210, 3, "BOOST COLOR", 108, KTN_FX_BUTTON_COLOUR, KTN_CAT_MISC},
  {0x1211, 3, "MOD COLOR", 108, KTN_FX_BUTTON_COLOUR, KTN_CAT_MISC},
  {0x1212, 3, "DLY1 COLOR", 108, KTN_FX_BUTTON_COLOUR, KTN_CAT_MISC},
  {0x1213, 3, "FX COLOR", 108, KTN_FX_BUTTON_COLOUR, KTN_CAT_MISC}, // 120
  {0x1214, 3, "RVB COLOR", 108, KTN_FX_BUTTON_COLOUR, KTN_CAT_MISC},
  {0x7430, 3, "L/OUT AIR", 267, FX_AMP_TYPE, KTN_CAT_GLOBAL}, // Address 0x7xxx is translated to 0x00000xxx
  {0x7431, 3, "CAB RESO", 270, FX_AMP_TYPE, KTN_CAT_GLOBAL},
  {0x7432, 2, "GLBL EQ SW", 0, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x743E, 2, "GL EQ POS", 272, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x7433, 18, "GEQ LOW CUT", 111, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x7434, 41, "GEQ LOW LVL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x7435, 26, "GEQ L-M F", 112, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x7436, 6, "GEQ L-M Q", 142, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x7437, 41, "GEQ L-M LVL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_GLOBAL}, // 130
  {0x7438, 26, "GEQ H-M F", 112, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x7439, 6, "GEQ H-M Q", 142, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x743A, 41, "GEQ H-M LVL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x743B, 15, "GEQ HI CUT", 127, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x743C, 41, "GEQ HI LVL", SHOW_CUT_BOOST, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
  {0x743D, 101, "GEQ LEVEL", SHOW_NUMBER, FX_FILTER_TYPE, KTN_CAT_GLOBAL},
};

const uint16_t KTN_NUMBER_OF_PARAMETERS = sizeof(KTN_parameters) / sizeof(KTN_parameters[0]);

#define KTN_FOOT_VOL_PARAMETER 116
#define KTN_MOD_SW_PARAMETER 15
#define KTN_MOD_TYPE_PARAMETER 16
#define KTN_FX_SW_PARAMETER 73
#define KTN_FX_TYPE_PARAMETER 74

struct KTN_fx_parameter_struct { // Combines all the data we need for controlling a parameter in a device
  uint8_t FX_no; // The number of the effect
  uint8_t NumVals; // The number of values for this parameter
  char Name[10]; // The name for the label
  uint16_t Sublist; // Which sublist to read for the FX or amp type - 0 if second byte does not contain the type or if there is no sublist +100 Show value from sublist.
};

// The following table contains the individual parameters of the MOD and FX effects.
// The parameters must be in blocks, otherwise the VController can not read them.
// The order is dictated by the addressing inside the Katana, so it should not be changed.
// Because of this, some lines are empty, because these parameters have no effect.
const PROGMEM KTN_fx_parameter_struct KTN_fx_parameters[] = {
  {0, 2,   "TW MODE", 150}, // "TOUCHWAH"
  {0, 2,   "TW POLR", 152},
  {0, 101, "TW SENS", SHOW_NUMBER},
  {0, 101, "TW FREQ", SHOW_NUMBER},
  {0, 101, "TW PEAK", SHOW_NUMBER},
  {0, 101, "TW D.MIX", SHOW_NUMBER},
  {0, 101, "TW FX.LVL", SHOW_NUMBER},
  {1, 2,   "AW MODE", 150}, // "AUTO WAH"
  {1, 101, "AW FREQ", SHOW_NUMBER},
  {1, 101, "AW PEAK", SHOW_NUMBER},
  {1, 101, "AW RATE", SHOW_NUMBER},
  {1, 101, "AW DEPTH", SHOW_NUMBER},
  {1, 101, "AW D.MIX", SHOW_NUMBER},
  {1, 101, "AW FX.LVL", SHOW_NUMBER},
  {2, 6,   "SW TYPE", 154}, // "SUB WAH"
  {2, 101, "SW P.POS", SHOW_NUMBER},
  {2, 101, "SW P.MIN", SHOW_NUMBER},
  {2, 101, "SW P.MAX", SHOW_NUMBER},
  {2, 101, "SW FX.LVL", SHOW_NUMBER},
  {2, 101, "SW D.LVL", SHOW_NUMBER},
  {3, 7,   "C TYPE", 160}, // "COMPRESSOR"
  {3, 101, "C SUSTAIN", SHOW_NUMBER},
  {3, 101, "C ATTACK", SHOW_NUMBER},
  {3, 101, "C TONE", SHOW_TONE_NUMBER},
  {3, 101, "C LEVEL", SHOW_NUMBER},
  {4, 3,   "L TYPE", 168}, // "LIMITER"
  {4, 101, "L ATTACK", SHOW_NUMBER},
  {4, 101, "L THRESH.", SHOW_NUMBER},
  {4, 101, "L RATIO", SHOW_NUMBER},
  {4, 101, "L RELEASE", SHOW_NUMBER},
  {4, 101, "L LEVEL", SHOW_NUMBER},
  {5, 22,  "DST TYPE", 1}, // "DISTORTION"
  {5, 121, "DST DRIVE", SHOW_NUMBER},
  {5, 101, "DS BOTTOM", SHOW_TONE_NUMBER},
  {5, 101, "DST TONE", SHOW_TONE_NUMBER},
  {5, 2,   "D SOLO SW", 0},
  {5, 101, "D SLO LVL", SHOW_NUMBER},
  {5, 101, "DS FX LVL", SHOW_NUMBER},
  {5, 101, "DIR LVL", SHOW_NUMBER},
  {6, 42,  "GEQ 31 Hz", SHOW_CUT_BOOST}, // "GRAPHIC EQ"
  {6, 42,  "GEQ 62 Hz", SHOW_CUT_BOOST},
  {6, 42,  "GEQ 125Hz", SHOW_CUT_BOOST},
  {6, 42,  "GEQ 250Hz", SHOW_CUT_BOOST},
  {6, 42,  "GEQ 500Hz", SHOW_CUT_BOOST},
  {6, 42,  "GEQ 1 kHz", SHOW_CUT_BOOST},
  {6, 42,  "GEQ 2 kHz", SHOW_CUT_BOOST},
  {6, 42,  "GEQ 4 kHz", SHOW_CUT_BOOST},
  {6, 42,  "GEQ 8 kHz", SHOW_CUT_BOOST},
  {6, 42,  "GEQ 16kHz", SHOW_CUT_BOOST},
  {6, 101, "GEQ LEVEL", SHOW_NUMBER},
  {7, 18,  "PEQ L.CUT", 111, }, // "PARAMETRIC EQ"
  {7, 41,  "PEQ L.LVL", SHOW_CUT_BOOST, },
  {7, 26,  "PE LM FRQ", 112, },
  {7, 6,   "PEQ LM Q", 142, },
  {7, 41,  "PE LM LVL", SHOW_CUT_BOOST, },
  {7, 26,  "PE HM FRQ", 112, },
  {7, 6,   "PEQ HM Q", 142, },
  {7, 41,  "PE HM LVL", SHOW_CUT_BOOST, },
  {7, 15,  "PEQ HICUT", 127, },
  {7, 41,  "PEQ HILVL", SHOW_CUT_BOOST, },
  {7, 101, "PEQ LEVEL", SHOW_NUMBER },
  {8, 8,   "TM TYPE", 171}, // "TONE MOD"
  {8, 101, "TM RESO", SHOW_NUMBER},
  {8, 101, "TM LOW", SHOW_TONE_NUMBER},
  {8, 101, "TM HIGH", SHOW_TONE_NUMBER},
  {8, 101, "TM LEVEL", SHOW_NUMBER},
  {9, 8,   "GS TYPE", 179}, // "GUITAR SIM"
  {9, 101, "GS LOW", SHOW_TONE_NUMBER},
  {9, 101, "GS HIGH", SHOW_TONE_NUMBER},
  {9, 101, "GS LEVEL", SHOW_NUMBER},
  {9, 101, "GS BODY", SHOW_NUMBER},
  {10, 101, "SG SENS", SHOW_NUMBER}, // SLOW GEAR
  {10, 101, "SG RISE T", SHOW_NUMBER},
  {10, 101, "SG LEVEL", SHOW_NUMBER},
  {11, 101, "DF TONE", SHOW_TONE_NUMBER}, // DEFRETTER
  {11, 101, "DF SENS", SHOW_NUMBER},
  {11, 101, "DF ATTACK", SHOW_NUMBER},
  {11, 101, "DF DEPTH", SHOW_NUMBER},
  {11, 101, "DF RESO", SHOW_NUMBER},
  {11, 101, "DF FX LVL", SHOW_NUMBER},
  {11, 101, "DF D.LVL", SHOW_NUMBER},
  {12, 2,   "WS WAVE", 187}, // WAVE SYNTH
  {12, 101, "WS CUTOFF", SHOW_NUMBER},
  {12, 101, "WS RESO", SHOW_NUMBER},
  {12, 101, "WS F.SENS", SHOW_NUMBER},
  {12, 101, "W F.DECAY", SHOW_NUMBER},
  {12, 101, "W F.DEPTH", SHOW_NUMBER},
  {12, 101, "WS FX LVL", SHOW_NUMBER},
  {12, 101, "WS D.MIX", SHOW_NUMBER},
  {13, 101, "SS TONE", SHOW_TONE_NUMBER}, // SITAR SIM
  {13, 101, "SS SENS", SHOW_NUMBER},
  {13, 101, "SS DEPTH", SHOW_NUMBER},
  {13, 101, "SS RESO", SHOW_NUMBER},
  {13, 101, "SS BUZZ", SHOW_NUMBER},
  {13, 101, "SS FX LVL", SHOW_NUMBER},
  {13, 101, "SS D.LVL", SHOW_NUMBER},
  {14, 4, "O RANGE", 189}, // OCTAVE
  {14, 101, "O LEVEL", SHOW_NUMBER},
  {14, 101, "O DIR.MIX", SHOW_NUMBER},
  {15, 2,   "PS VOICE1", 193}, // PITCH SHIFTER
  {15, 4,   "PS MODE 1", 195},
  {15, 50,  "PS PITCH1", SHOW_PITCH_NUMBER},
  {15, 101, "PS FINE 1", SHOW_TONE_NUMBER},
  {15, TIME_300, "PS PREDLY", SHOW_DELAY_TIME},
  {15, 0, "--", 0},
  {15, 101, "PS LEVEL1", SHOW_NUMBER},
  {15, 2, "PS VOICE2", 193},
  {15, 4, "PS MODE 2", 195},
  {15, 50, "PS PITCH2", SHOW_PITCH_NUMBER},
  {15, 101, "PS FINE 2", SHOW_TONE_NUMBER},
  {15, TIME_300, "PS PREDLY", SHOW_DELAY_TIME},
  {15, 0, "--", 0},
  {15, 101, "PS F.BACK", SHOW_NUMBER},
  {15, 101, "PS LEVEL2", SHOW_NUMBER},
  {15, 101, "PS D.MIX", SHOW_NUMBER},
  {16, 2, "H VOICE", 193}, // HARMONIST
  {16, 30, "H HARM 1", 199},
  {16, TIME_300, "H PRE-DLY", SHOW_DELAY_TIME},
  {16, 12, "H KEY", 255}, // Is translated to master key!
  {16, 101, "H LEVEL 1", SHOW_NUMBER},
  {16, 30, "H HARM 2", 199},
  {16, TIME_300, "H PRE-DLY", SHOW_DELAY_TIME},
  {16, 0, "--", 0},
  {16, 101, "H LEVEL 2", SHOW_NUMBER},
  {16, 101, "H F.BACK", SHOW_NUMBER},
  {16, 101, "H DIR.MIX", SHOW_NUMBER},
  {17, 2, "SH HOLD", 0}, // SOUND HOLD
  {17, 101, "SH RISE T", SHOW_NUMBER},
  {17, 121, "SH LEVEL", SHOW_NUMBER},
  {18, 4, "AP TYPE", 229}, // AC. PROCESSOR
  {18, 101, "AP BASS", SHOW_TONE_NUMBER, },
  {18, 101, "AP MIDDLE", SHOW_TONE_NUMBER, },
  {18, 26, "AP M FRQ", 112 },
  {18, 101, "AP TREBLE", SHOW_TONE_NUMBER, },
  {18, 101, "AP PRES.", SHOW_TONE_NUMBER, },
  {18, 101, "AP LEVEL", SHOW_NUMBER },
  {19, 4, "PH TYPE", 233}, // "PHASER"
  {19, 101, "PH RATE", SHOW_NUMBER },
  {19, 101, "PH DEPTH", SHOW_NUMBER },
  {19, 101, "PH MANUAL", SHOW_NUMBER },
  {19, 101, "PH RESO", SHOW_NUMBER },
  {19, 101, "P STEP RT", SHOW_NUMBER },
  {19, 101, "PH FX LVL", SHOW_NUMBER },
  {19, 101, "PH D.LVL", SHOW_NUMBER },
  {20, 101, "FL RATE", SHOW_NUMBER }, // FLANGER
  {20, 101, "FL DEPTH", SHOW_NUMBER },
  {20, 101, "FL MANUAL", SHOW_NUMBER },
  {20, 101, "FL RESO", SHOW_NUMBER },
  {20, 101, "FL SEPRTE", SHOW_NUMBER },
  {20, 18, "FL LOW CT", 111, },
  {20, 101, "FL FX LVL", SHOW_NUMBER },
  {20, 101, "FL D.LVL", SHOW_NUMBER },
  {21, 101, "TRM SHAPE", SHOW_NUMBER }, // TREMOLO
  {21, 101, "TRM RATE", SHOW_NUMBER },
  {21, 101, "TRM DEPTH", SHOW_NUMBER },
  {21, 101, "TRM LEVEL", SHOW_NUMBER },
  {22, 101, "ROT RATE", SHOW_NUMBER }, // ROTARY 11
  {22, 0, "--", 0 },
  {22, 0, "--", 0 },
  {22, 101, "ROT DEPTH", SHOW_NUMBER },
  {22, 101, "ROT LEVEL", SHOW_NUMBER },
  {23, 101, "U-V RATE", SHOW_NUMBER }, // UNI-V
  {23, 101, "U-V DEPTH", SHOW_NUMBER },
  {23, 101, "U-V LEVEL", SHOW_NUMBER },
  {24, 2, "PAN TYPE", 239 }, // PAN
  {24, 101, "PAN POS", SHOW_PAN },
  {24, 101, "P W.SHAPE", SHOW_NUMBER },
  {24, 101, "PAN RATE", SHOW_NUMBER },
  {24, 101, "PAN DEPTH", SHOW_NUMBER },
  {24, 101, "PAN LEVEL", SHOW_NUMBER },
  {25, 20, "S PATTERN", SHOW_NUMBER_PLUS_ONE }, // SLICER
  {25, 100, "SL RATE", SHOW_NUMBER },
  {25, 100, "S TR.SENS", SHOW_NUMBER },
  {25, 100, "SL FX LVL", SHOW_NUMBER },
  {25, 100, "SL D.LVL", SHOW_NUMBER },
  {26, 100, "VIB RATE", SHOW_NUMBER }, // VIBRATO
  {26, 100, "VIB DEPTH", SHOW_NUMBER },
  {26, 100, "VIB TRIG.", SHOW_NUMBER },
  {26, 100, "V RISE T.", SHOW_NUMBER },
  {26, 100, "VIB LEVEL", SHOW_NUMBER },
  {27, 2, "RMOD MODE", 241 }, // RING MOD
  {27, 100, "RMOD FREQ", SHOW_NUMBER },
  {27, 100, "RMOD FLVL", SHOW_NUMBER },
  {27, 100, "RMOD DMIX", SHOW_NUMBER },
  {28, 2, "H MODE", 243 }, // HUMANIZER
  {28, 5, "H VOWEL1", 245 },
  {28, 5, "H VOWEL2", 245 },
  {28, 101, "H SENS", SHOW_NUMBER },
  {28, 101, "H RATE", SHOW_NUMBER },
  {28, 101, "H DEPTH", SHOW_NUMBER },
  {28, 101, "H MANUAL", SHOW_NUMBER },
  {28, 101, "H LEVEL", SHOW_NUMBER },
  {29, 16, "2 X-OverF", 119 }, // 2x2 CHORUS
  {29, 101, "2 L.RATE", SHOW_NUMBER },
  {29, 101, "2 L.DEPTH", SHOW_NUMBER },
  {29, 81, "2 L.PreD", SHOW_MILLIS },
  {29, 101, "2 L.LVL", SHOW_NUMBER },
  {29, 101, "2 H.RATE", SHOW_NUMBER },
  {29, 101, "2 H.DEPTH", SHOW_NUMBER },
  {29, 81, "2 H.PreD", SHOW_MILLIS },
  {29, 101, "2 H.LVL", SHOW_NUMBER },
  {30, 2, "DLY TYPE", 250 }, // SUB DELAY
  {30, TIME_1000, "DLY TIME", SHOW_DELAY_TIME },
  {30, 0, "--", 0 },
  {30, 101, "DLY FBACK", SHOW_NUMBER },
  {30, 15,  "DLY H.CUT", 127 },
  {30, 101, "DLY FXLVL", SHOW_NUMBER },
  {30, 101, "DLY D.MIX", SHOW_NUMBER },
  {31, 101, "AG HIGH", SHOW_TONE_NUMBER }, // Ac. GUITARSIM
  {31, 101, "AG BODY", SHOW_NUMBER },
  {31, 101, "AG LOW", SHOW_TONE_NUMBER },
  {31, 101, "AG LEVEL", SHOW_NUMBER },
  {32, 101, "ROT2 BAL", SHOW_NUMBER }, // ROTARY 2
  {32, 2, "ROT2SPEED", 237 },
  {32, 101, "ROT2 RT-S", SHOW_NUMBER },
  {32, 101, "ROT2 RT-F", SHOW_NUMBER },
  {32, 101, "ROT2 RISE", SHOW_NUMBER },
  {32, 101, "ROT2 FALL", SHOW_NUMBER },
  {32, 101, "ROT2 DPTH", SHOW_NUMBER },
  {32, 101, "ROT2 LVL", SHOW_NUMBER },
  {33, 3, "TE MODE", 252 }, // TERA ECHO
  {33, 101, "TE S.TIME", SHOW_NUMBER },
  {33, 101, "TE F.BACK", SHOW_NUMBER },
  {33, 101, "TE TONE", SHOW_TONE_NUMBER },
  {33, 101, "TE FX LVL", SHOW_NUMBER },
  {33, 101, "TE D.LVL", SHOW_NUMBER },
  {33, 2, "TE HOLD", 0 },
  {34, 101, "OT DETUNE", SHOW_NUMBER }, // OVERTONE
  {34, 101, "OT TONE", SHOW_TONE_NUMBER },
  {34, 101, "OT UPPER", SHOW_NUMBER },
  {34, 101, "OT LOWER", SHOW_NUMBER },
  {34, 101, "OT DIRECT", SHOW_NUMBER },
  {35, 2, "90 SCRIPT", 0 }, // PHASER 90E
  {35, 101, "90E SPEED", SHOW_NUMBER },
  {36, 101, "119MANUAL", SHOW_NUMBER }, // FLANGER 119E
  {36, 101, "119WIDTH", SHOW_NUMBER },
  {36, 101, "119SPEED", SHOW_NUMBER },
  {36, 101, "119REGEN.", SHOW_NUMBER },
  {37, 101, "95E P.POS", SHOW_NUMBER },  // WAH 95E
  {37, 101, "95E P.MIN", SHOW_NUMBER },
  {37, 101, "95E P.MAX", SHOW_NUMBER },
  {37, 101, "95E F.LVL", SHOW_NUMBER },
  {37, 101, "95E D.LVL", SHOW_NUMBER },
  {38, 2,   "DC SELECT", 283 },          // DC30
  {38, 101, "DC INPUT", SHOW_NUMBER },
  {38, 101, "CH:INTSTY", SHOW_NUMBER },
  {38, RPT_600, "EC:RPT R", SHOW_DELAY_TIME },
  {38, 0, "--", 0 },
  {38, 101, "EC:INSTY", SHOW_NUMBER },
  {38, 101, "EC:VOL", SHOW_NUMBER },
  {38, 101, "DC TONE", SHOW_NUMBER },
  {38, 2, "DC OUT SL", 285 },
};

const uint16_t KTN_NUMBER_OF_FX_PARAMETERS = sizeof(KTN_fx_parameters) / sizeof(KTN_fx_parameters[0]);

const PROGMEM char KTN_sublists[][9] = {
  // Sublist 1 - 22: Booster types
  "MILD B", "CLEAN B", "TREBLE B", "CRUNCH", "NAT OD", "WARM OD", "FAT DS", "LEAD DS", "METAL DS", "OCT.FUZZ",
  "BLUES OD", "OD-1", "TUBESCRM", "TURBO OD", "DIST", "RAT", "GUVNR DS", "DST+", "METAL ZN", "60s FUZZ",
  "MUFF FZ", "CUSTOM",

  // Sublist 23 - 61: MOD/FX types
  "TOUCHWAH", "AUTO WAH", "SUB WAH", "COMPRSOR", "LIMITER", "DIST", "GRAPH EQ", "PARAM EQ", "TONE MOD", "GTR SIM",
  "SLOW GR", "DEFRET", "WAV SNTH", "SITARSIM", "OCTAVE", "PITCH SH", "HARMONST", "SND HOLD", "AC. PROC", "PHASER",
  "FLANGER", "TREMOLO", "ROTARY 1", "UNI-V", "PAN", "SLICER", "VIBRATO", "RING MOD", "HUMANIZR", "2x2 CHOR",
  "SUB DLY", "AcGtrSim", "ROTARY 2", "TeraEcho", "OVERTONE", "PHAS 90E", "FLGR117E", "WAH 95E", "DC30", // Tera Echo and Overtone do not work for the MOD FX

  // Sublist 62 - 89 : Amp types
  "NAT CLN", "ACOUSTIC", "COMBO C", "STACK C", "HiGAIN", "POWER DR", "EXTREME", "CORE MTL", "CLEAN!", "CLN TWIN",
  "PRO CR", "CRUNCH!", "DELUXE 2", "VO DRIVE", "VO LEAD", "MATCH DR", "BG LEAD", "BG DRIVE", "MS1959I", "MS1959II",
  "RFIER V", "RFIER M", "T-AMP L", "BROWN!", "LEAD!", "CUSTOM", "BGNR UBR", "ORNGE RR",

  // Sublist 90 - 100 : Delay types
  "DIGITAL", "PAN", "STEREO", "DUAL SER", "DUAL PAR", "DUAL L/R", "REVERSE", "ANALOG", "TAPE ECO", "MODULATE",
  "SDE-3000",

  // Sublist 101 - 107 : Reverb types
  "AMBIENCE", "ROOM", "HALL 1", "HALL 2", "PLATE", "SPRING", "MODULATE",

  // Sublist 108 - 110 : FX color types
  "GREEN", "RED", "YELLOW",

  // Sublist 111 - 141: Frequencies
  // Low: start at 111 -
  // Mid - start at 112
  // High - start at 127
  "FLAT", "20.0Hz", "25.0Hz", "31.5Hz", "40.0Hz", "50.0Hz", "63.0Hz", "80.0Hz", "100Hz", "125Hz",
  "160Hz", "200Hz", "250Hz", "315Hz", "400Hz", "500Hz", "630Hz", "800Hz", "1.00kHz", "1.25kHz",
  "1.60kHz", "2.0kHz", "2.50kHz", "3.15kHz", "4.00kHz", "5.00kHz", "6.30kHz", "8.00kHz", "10.0kHz", "12.5kHz",
  "FLAT",

  // Sublist 142 - 147 - Mid Q
  "0,5", "1", "2", "4", "8", "16",

  // Sublist 148 - 149 - S/R mode
  "SERIES", "PARALLEL",

  // Sublist 150 - 151: Wah Mode
  "LPF", "BPF",

  // Sublist 152 - 153 T.Wah polar
  "DOWN", "UP",

  // Sublist 154 - 159 Wah types
  "CRY WAH", "VO WAH", "FAT WAH", "LITE WAH", "7STR.WAH", "RESO WAH",

  // Sublist 160 - 167 Compressor types
  "BOSS CMP", "HI-BAND", "LIGHT", "D-COMP", "ORANGE", "FAT", "MILD", "ST COMP",

  // Sublist 168 - 170: Limiter types
  "BOSS LIM",  "RACK160D", "VTGRACKU",

  // Sublist 171 - 178: Tone MOD types
  "FAT", "PRESENCE", "MILD", "TIGHT", "ENHANCE", "RESO1", "RESO2", "RESO3",

  // Sublist 179 - 186: Guitar sim types
  "S->H", "H->S", "H->HF", "S->HLW", "H->HLW", "S->AC", "H->AC", "P->AC",

  // Sublist 187 - 188: Wave synth WAVE types
  "SAW", "SQUARE",

  // Sublist 189 - 192: Octave ranges
  "1: B1-E6", "2:B1-E5", "3:B1-E4", "4:B1-E3",

  // Sublist 193 - 194: Pitch shift voices
  "1-VOICE", "2-MONO",

  // Sublist 195 - 198: Pitch shift modes
  "FAST", "MEDIUM", "SLOW", "MONO",

  // Sublist 199 - 228
  "-2oct", "-14th", "-13th", "-12th", "-11th", "-10th", "-9th",
  "-1oct", "-7th", "-6th", "-5th", "-4th", "-3rd", "-2nd", "TONIC",
  "+2nd", "+3rd", "+4th", "+5th", "+6th", "+7th", "+1oct", "+9th", "+10th", "+11th",
  "+12th", "+13th", "+14th", "+2oct", "USER",

  // Sublist 229 - 232: Ac processor types
  "SMALL", "MEDIUM", "BRIGHT", "POWER",

  // Sublist 233 - 236: Phaser types
  "4STAGE", "8STAGE", "12STAGE", "BiPHASE",

  // Sublist 237 - 238: Rotary speed
  "SLOW", "FAST",

  // Sublist 239 - 240 PAN types
  "AUTO", "MANUAL",

  // Sublist 241 -242 RING MOD modes
  "NORMAL", "INTLGNT",

  // Sublist 243 -244 HUMANIZER modes
  "PICKING", "AUTO",

  // Sublist 245 -249 HUMANIZER vowels
  "a", "e", "i", "o", "u",

  // 250 - 251 SUB DELAY types
  "MONO", "PAN",

  // 252 - 253 TERA ECHO MODES
  "MONO", "ST1", "ST2",

  // Sublist 254 - 266 for key
  "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",

  // Sublist 267 - 269 for Line Out Live Feel
  "REC", "LIVE", "BLEND",

  // Sublist 270 - 272 for Cabinet Resonance
  "VINTAGE", "MODERN", "DEEP",

  // Sublist 273 - 274 for Global eq position
  "INPUT", "OUTPUT",

  // Sublist 275 - 282 for Boost Custom types
  "OD-1", "OD-2", "CRUNCH", "DS-1", "DS-2", "METAL-1", "METAL-2", "FUZZ",

  // Sublist 283 - 284 for DC30 selector
  "CHORUS", "ECHO",

  // Sublist 285 - 286 for DC30 output select
  "D+E", "D/E",

  // Sublist 287 - 288 for EQ type
  "PARAMTRC", "GRAPHIC",
};

const uint16_t KTN_SIZE_OF_SUBLIST = sizeof(KTN_sublists) / sizeof(KTN_sublists[0]);

const PROGMEM uint8_t KTN_FX_colours[KTN_NUMBER_OF_FX_TYPES] = { // Table with the LED colours for the different FX states
  FX_WAH_TYPE, // Colour for "TOUCHWAH"
  FX_WAH_TYPE, // Colour for "AUTO WAH"
  FX_WAH_TYPE, // Colour for "SUB WAH"
  FX_DYNAMICS_TYPE, // Colour for "COMPRSOR"
  FX_DYNAMICS_TYPE, // Colour for "LIMITER"
  FX_DIST_TYPE, // Colour for "DIST"
  FX_FILTER_TYPE, // Colour for "GRAPH EQ"
  FX_FILTER_TYPE, // Colour for "PARAM EQ"
  FX_FILTER_TYPE, // Colour for "TONE MOD"
  FX_GTR_TYPE, // Colour for "GTR SIM",
  FX_GTR_TYPE, // Colour for "SLOW GR"
  FX_GTR_TYPE, // Colour for "DEFRET"
  FX_GTR_TYPE, // Colour for "WAV SNTH"
  FX_GTR_TYPE, // Colour for "SITARSIM"
  FX_PITCH_TYPE, // Colour for "OCTAVE"
  FX_PITCH_TYPE, // Colour for "PITCH SH"
  FX_PITCH_TYPE, // Colour for "HARMONST"
  FX_GTR_TYPE, // Colour for "SND HOLD"
  FX_GTR_TYPE, // Colour for "AC. PROC"
  FX_MODULATE_TYPE, // Colour for "PHASER",
  FX_MODULATE_TYPE, // Colour for "FLANGER"
  FX_MODULATE_TYPE, // Colour for "TREMOLO"
  FX_MODULATE_TYPE, // Colour for "ROTARY 1"
  FX_MODULATE_TYPE, // Colour for "UNI-V"
  FX_MODULATE_TYPE, // Colour for "PAN"
  FX_MODULATE_TYPE, // Colour for "SLICER"
  FX_MODULATE_TYPE, // Colour for "VIBRATO"
  FX_MODULATE_TYPE, // Colour for "RING MOD"
  FX_MODULATE_TYPE, // Colour for "HUMANIZR"
  FX_MODULATE_TYPE, // Colour for "2x2 CHOR",
  FX_DELAY_TYPE, // Colour for  "SUB DLY"
  FX_GTR_TYPE, // Colour for "AcGtrSim"
  FX_MODULATE_TYPE, // Colour for "ROTARY 2"
  FX_DELAY_TYPE, // Colour for "TERA ECHO"
  FX_MODULATE_TYPE, // Colour for "OVERTONE"
  FX_MODULATE_TYPE, // Colour for "PHAS 90E"
  FX_MODULATE_TYPE, // Colour for "Flgr117E",
  FX_WAH_TYPE, // Colour for "WAH 95E"
  FX_DELAY_TYPE, // Colour for "DC30"
};

const uint8_t KTN_FX_button_colours[3] = { // Table with the LED colours for the different FX states
  1, // Green
  2, // Red
  7  // Yellow
};

uint8_t KTN_number_of_items_in_category[KTN_NUMBER_OF_FX_CATEGORIES];
uint16_t KTN_first_fx_item[KTN_NUMBER_OF_FX_TYPES];

void MD_KTN_class::count_parameter_categories() {
  uint8_t c;
  for (uint16_t i = 0; i < KTN_NUMBER_OF_PARAMETERS; i++) {
    c = KTN_parameters[i].Category;
    if (c > 0) KTN_number_of_items_in_category[c - 1]++;
  }

  for (uint16_t i = KTN_NUMBER_OF_FX_PARAMETERS; i -- > 0;) {
    uint16_t item = KTN_fx_parameters[i].FX_no;
    KTN_first_fx_item[item] = i;
  }
}

void MD_KTN_class::request_par_bank_category_name(uint8_t sw) {
  uint8_t index = SP[sw].PP_number;
  if ((index > 0) && (index <= KTN_NUMBER_OF_FX_CATEGORIES))
    LCD_set_SP_label(sw, (const char*) &KTN_parameter_category[index - 1].Name);
  else LCD_clear_SP_label(sw);
}

#define NO_FX_PARAMETER 65535

uint16_t MD_KTN_class::get_fx_table_index(uint16_t number) {
  uint8_t fx_type;
  //if (number >= KTN_NUMBER_OF_PARAMETERS) return NO_FX_PARAMETER;

  uint32_t address = KTN_parameters[number].Address;
  if (address < 0x8000) return NO_FX_PARAMETER;

  if ((address & 0xF000) == 0x8000) { // mod parameter requested
    fx_type = current_mod_type;
  }
  else if ((address & 0xF000) == 0x9000) { // FX parameter requested
    fx_type = current_fx_type;
  }
  else return NO_FX_PARAMETER;

  uint16_t index = KTN_first_fx_item[fx_type];
  index += address & 0x000F; // We use the most right byte of the address as index of which byte is requested.

  return index;
}

void MD_KTN_class::read_parameter_name(uint16_t number, String &Output) { // Called from menu
  if (number < number_of_parameters())  {
    uint16_t fx_index = get_fx_table_index(number);
    uint8_t fx_no;
    if (fx_index == NO_FX_PARAMETER) { // Regular parameter
      Output = KTN_parameters[number].Name;
    }
    else { // Mod or FX parameter
      if ((KTN_parameters[number].Address & 0xF000) == 0x8000) { // Check if number points to a MOD parameter
        Output = "MOD ";
        fx_no = current_mod_type;
      }
      else { // Then it must be an FX parameter
        Output = "FX ";
        fx_no = current_fx_type;
      }
      if (KTN_fx_parameters[fx_index].FX_no == fx_no) Output += KTN_fx_parameters[fx_index].Name;
      else Output += "--";
    }
  }
  else Output = "?";
}

void MD_KTN_class::read_parameter_value_name(uint16_t number, uint16_t value, String &Output) {
  if (number < number_of_parameters())  {
    uint16_t fx_index = get_fx_table_index(number);
    uint16_t sublist;
    if (fx_index == NO_FX_PARAMETER) {
      sublist = KTN_parameters[number].Sublist;
    }
    else {
      sublist = KTN_fx_parameters[fx_index].Sublist;
    }
    if (sublist > 0) { // Check if state needs to be read
      switch (sublist) {
        case SHOW_NUMBER:
        case SHOW_DELAY_TIME:
          Output += String(value);
          break;
        case SHOW_TONE_NUMBER:
          if (value > 50) Output += "+";
          Output += String(value - 50);
          break;
        case SHOW_PAN:
          if (value < 64) Output += "L" + String(50 - value);
          if (value == 64) Output += "C";
          if (value > 64) Output += "R" + String(value - 50);
          break;
        case SHOW_CUT_BOOST:
          if (value > 20)  Output += "+";
          Output += String(value - 20);
          Output += "dB";
          break;
        case SHOW_RVB_TIME:
          value++;
          Output += String(value / 10);
          Output += ".";
          Output += String(value % 10);
          Output += "sec";
          break;
        case SHOW_PITCH_NUMBER:
          if (value > 24)  Output += "+";
          Output += String(value - 24);
          break;
        case SHOW_MILLIS:
          Output += String(value / 2);
          Output += ".";
          Output += String((value & 1) * 5);
          Output += "ms";
          break;
        case SHOW_NUMBER_PLUS_ONE:
          Output += String(value + 1);
          break;
        default:
          String type_name = KTN_sublists[sublist + value - 1];
          Output += type_name;
          break;
      }
    }
    else if (value == 1) Output += "ON";
    else Output += "OFF";
  }
  else Output += "?";
}

uint32_t MD_KTN_class::parameter_address(uint8_t number) {
  if (number >= KTN_NUMBER_OF_PARAMETERS) return 0;
  uint32_t my_address = KTN_parameters[number].Address;
  if ((my_address & 0xF000) == 0x8000) {
    if ((uint8_t)(my_address & 0x000F) >= (KTN_fx_memory[current_mod_type].Length)) return 0;
    my_address = (my_address & 0x000F) + KTN_fx_memory[current_mod_type].Mod_address;
    if (my_address & 0x0080) my_address = (my_address & 0x7F7F) + 0x0100; // Fix 7 bit calculation error
    if (my_address == 0x0248) my_address = 0x0718; // Show master key parameter in harmonizer fx parameters.
    DEBUGMSG("MOD PARAMETER ADDRESS: " + String(KTN_parameters[number].Address & 0x000F) + "+" + String(KTN_fx_memory[current_mod_type].Mod_address) + "=" + String(my_address) );
    return my_address + 0x60000000;
  }
  if ((my_address & 0xF000) == 0x9000) {
    if ((uint8_t)(my_address & 0x007F) >= (KTN_fx_memory[current_fx_type].Length)) return 0;
    my_address = (my_address & 0x007F) + KTN_fx_memory[current_fx_type].FX_address;
    if (my_address & 0x0080) my_address = (my_address & 0x7F7F) + 0x0100; // Fix 7 bit calculation error
    if (my_address == 0x0454) my_address = 0x0718; // Show master key parameter in harmonizer mod parameters.
    DEBUGMSG("FX PARAMETER ADDRESS: " + String(my_address) );
    return my_address + 0x60000000;
  }
  if ((my_address & 0xF000) == 0x7000) return (my_address & 0xFFF); // For global settings
  return my_address + 0x60000000;
}

// Toggle KTN stompbox parameter
void MD_KTN_class::parameter_press(uint8_t Sw, Cmd_struct *cmd, uint16_t number) {

  if (number >= KTN_NUMBER_OF_PARAMETERS) return;

  // Send sysex MIDI command to KATANA
  uint16_t value = SCO_return_parameter_value(Sw, cmd);

  // Skip non implemented mod types (Tera echo and Overtone)
  if ((KTN_parameters[number].Address == 0x0141) && (value == 33)) value += 2;

  if (SP[Sw].Latch != TGL_OFF) {
    if (number_of_values(number) < 128) write_sysex(parameter_address(number), value); // Writing single value
    else write_sysex(parameter_address(number), SP[Sw].Target_byte1, SP[Sw].Target_byte2); // Writing double value (used for delay times)
    SP[Sw].Offline_value = value;

    // Show message
    check_update_label(Sw, value);
    String msg = "";
    if (SP[Sw].Type != ASSIGN) {
      //msg = KTN_parameters[number].Name;
      read_parameter_title(number, msg);
      if (!(KTN_parameters[number].Sublist & SUBLIST_FROM_BYTE2)) {
        msg += ":";
        msg += SP[Sw].Label;
      }
      else { // Sublist from byte 2 - only used for toggle FX
        msg += SP[Sw].Label;
        msg.trim();
        msg += ':';
        if (value == 1) msg += "ON";
        else msg += "OFF";
      }
      LCD_show_popup_label(msg, ACTION_TIMER_LENGTH);
    }

    // Update Expr pedal selection if neccesary
    auto_toggle_exp_pedal(number, value);

    //PAGE_load_current(false); // To update the other parameter states, we re-load the current page
    if (SP[Sw].Latch != UPDOWN) update_page = REFRESH_FX_ONLY;
  }
}

void MD_KTN_class::parameter_release(uint8_t Sw, Cmd_struct *cmd, uint16_t number) {

  // Work out state of pedal
  if (SP[Sw].Latch == MOMENTARY) {
    SP[Sw].State = 2; // Switch state off
    write_sysex(parameter_address(number), cmd->Value2);
    SP[Sw].Offline_value = cmd->Value2;

    //PAGE_load_current(false); // To update the other switch states, we re-load the current page
    update_page = REFRESH_FX_ONLY;
  }
}

void MD_KTN_class::read_parameter_title(uint16_t number, String &Output) {
  uint16_t fx_index = get_fx_table_index(number);
  if (fx_index == NO_FX_PARAMETER) {
    Output += KTN_parameters[number].Name;
  }
  else {
    uint32_t my_address = parameter_address(number); // Will return zero, if there is no parameter
    if (my_address != 0) {
      Output += KTN_fx_parameters[fx_index].Name;
    }
    else {
      Output += "--"; // No valid parameter
    }
  }
}

bool MD_KTN_class::request_parameter(uint8_t sw, uint16_t number) {
  if (can_request_sysex_data()) {
    uint32_t my_address = parameter_address(number); // Will return zero, if there is no parameter
    if (my_address == 0) {
      LCD_clear_SP_label(sw);
      SP[sw].Latch = TGL_OFF;
      SP[sw].Colour = 0;
      return true;
    }
    last_requested_sysex_address = my_address;
    last_requested_sysex_type = REQUEST_PARAMETER_TYPE;
    last_requested_sysex_switch = sw;
    request_sysex(my_address, 2); // Request the parameter state data
    return false; // Move to next switch is false. We need to read the parameter first
  }
  else {
    if ((sw < TOTAL_NUMBER_OF_SWITCHES) && (SP[sw].Type == PAR_BANK)) read_parameter(sw, SP[sw].Offline_value, SP[sw + 1].Offline_value);
    else read_parameter(sw, SP[sw].Offline_value, 0);
    return true;
  }
}

void MD_KTN_class::read_parameter(uint8_t sw, uint8_t byte1, uint8_t byte2) { //Read the current KTN parameter
  SP[sw].Target_byte1 = byte1;
  SP[sw].Target_byte2 = byte2;

  // Set the status
  SP[sw].State = SCO_find_parameter_state(sw, byte1);

  // Set the colour
  uint16_t index = SP[sw].PP_number; // Read the parameter number (index to KTN-parameter array)
  uint8_t my_colour = KTN_parameters[index].Colour;

  //Check for special colours:
  if (my_colour == KTN_FX_COLOUR) SP[sw].Colour = KTN_FX_colours[byte2]; //FX type read in byte2
  else if (my_colour == KTN_MOD_TYPE_COLOUR) SP[sw].Colour = KTN_FX_colours[current_mod_type]; //FX type read in byte1
  else if (my_colour == KTN_FX_TYPE_COLOUR) SP[sw].Colour = KTN_FX_colours[current_fx_type]; //FX type read in byte1
  else if (my_colour == KTN_FX_BUTTON_COLOUR) SP[sw].Colour = KTN_FX_button_colours[byte1];
  else SP[sw].Colour =  my_colour;

  // Set the display message
  uint16_t fx_index = get_fx_table_index(index);
  String msg = "";
  uint16_t sublist;
  if (fx_index == NO_FX_PARAMETER) {
    sublist = KTN_parameters[index].Sublist;
  }
  else {
    sublist = KTN_fx_parameters[fx_index].Sublist;
  }

  if (sublist > SUBLIST_FROM_BYTE2) { // Check if a sublist exists
    //msg.remove(msg.length() - 1); // Remove the last character (semicolon)
    String type_name = KTN_sublists[sublist - SUBLIST_FROM_BYTE2 + byte2 - 1];
    msg += "(" + type_name + ")";
  }
  if (sublist == SHOW_DELAY_TIME) {
    //msg += ":";
    read_parameter_value_name(index, (128 * byte1) + byte2 , msg);
  }
  else if ((sublist > 0) && !(sublist & SUBLIST_FROM_BYTE2)) { // Check if state needs to be read
    //String type_name = KTN_sublists[KTN_parameters[index].Sublist + byte1 - 101];
    //msg += ":";
    read_parameter_value_name(index, byte1, msg);
  }
  //Copy it to the display name:
  LCD_set_SP_label(sw, msg);
  update_lcd = sw;
  //LCD_update(sw, true);
}

void MD_KTN_class::check_update_label(uint8_t Sw, uint16_t value) { // Updates the label for extended sublists
  uint16_t index = SP[Sw].PP_number; // Read the parameter number (index to KTN-parameter array)
  if ((index != NOT_FOUND) && (index < KTN_NUMBER_OF_PARAMETERS)) {
    String msg = "";
    uint16_t sublist;
    uint16_t fx_index = get_fx_table_index(index);
    if (fx_index == NO_FX_PARAMETER) {
      sublist = KTN_parameters[index].Sublist;
    }
    else {
      sublist = KTN_fx_parameters[fx_index].Sublist;
    }
    if ( !(sublist & SUBLIST_FROM_BYTE2)) { // Check if state needs to be read
      LCD_clear_SP_label(Sw);
      // Set the display message
      //msg += ":";
      read_parameter_value_name(index, value, msg);

      //Copy it to the display name:
      LCD_set_SP_label(Sw, msg);

      //Update the current switch label
      update_lcd = Sw;
    }
  }
}

uint16_t MD_KTN_class::number_of_parameters() {
  return KTN_NUMBER_OF_PARAMETERS;
}

uint16_t MD_KTN_class::number_of_parbank_parameters() {
  if (parameter_bank_category == 0) return KTN_NUMBER_OF_PARAMETERS;
  else return KTN_number_of_items_in_category[parameter_bank_category - 1];
}

uint16_t MD_KTN_class::get_parbank_parameter_id(uint16_t par_number) {
  if (parameter_bank_category == 0) return par_number; // In category 0 all FX are accessible

  //Find the correct parameter number for this parameter number
  uint8_t active_fx_number = 0;
  for (uint8_t i = 0; i < KTN_NUMBER_OF_PARAMETERS; i++) {
    if (KTN_parameters[i].Category == parameter_bank_category) {
      if (active_fx_number == par_number) return i;
      active_fx_number++;
    }
  }
  return 65535; // No parameters in this category
}

uint8_t MD_KTN_class::number_of_values(uint16_t index) {

  if (index >= KTN_NUMBER_OF_PARAMETERS) return 0;
  uint16_t fx_index = get_fx_table_index(index);
  if (fx_index == NO_FX_PARAMETER) {
    return KTN_parameters[index].NumVals;
  }
  else {
    return KTN_fx_parameters[fx_index].NumVals;
  }
}

void MD_KTN_class::par_bank_updown(signed int delta, uint8_t my_bank_size) {
  // We need a special version of par_bank_updown for the Katana to skip the empty parameters

  if (my_bank_size != 1) {
    MD_base_class::par_bank_updown(delta, my_bank_size); // Perform the base class version
    return;
  }

  // Perform bank up:
  if (delta > 0) {
    for (uint8_t i = 0; i < delta; i++) {
      do {
        if (parameter_bank_number >= (number_of_parbank_parameters() - 1) / my_bank_size) parameter_bank_number = 0; // Check if we've reached the top
        else parameter_bank_number++; //Otherwise move bank up
      } while (check_parameter_empty(parameter_bank_number));
    }
  }
  // Perform bank down:
  if (delta < 0) {
    for (uint8_t i = 0; i < abs(delta); i++) {
      do {
        if (parameter_bank_number <= 0) parameter_bank_number = (number_of_parbank_parameters() - 1) / my_bank_size; // Check if we've reached the bottom
        else parameter_bank_number--; //Otherwise move bank down
      } while (check_parameter_empty(parameter_bank_number));
    }
  }

  String msg = "";
  read_parameter_name(parameter_bank_number, msg);
  if (parameter_bank_number == KTN_MOD_SW_PARAMETER) {
    msg += " (";
    msg += KTN_sublists[current_mod_type + 22];
    msg += ')';
  }
  if (parameter_bank_number == KTN_MOD_TYPE_PARAMETER) {
    msg += ':';
    msg += KTN_sublists[current_mod_type + 22];
  }
  if (parameter_bank_number == KTN_FX_SW_PARAMETER) {
    msg += " (";
    msg += KTN_sublists[current_fx_type + 22];
    msg += ')';
  }
  if (parameter_bank_number == KTN_FX_TYPE_PARAMETER) {
    msg += ':';
    msg += KTN_sublists[current_fx_type + 22];
  }
  LCD_show_popup_label(msg, MESSAGE_TIMER_LENGTH);

  update_page = REFRESH_PAGE; //Re-read the patchnames for this bank
}

bool MD_KTN_class::check_parameter_empty(uint16_t number) {
  uint32_t my_address = KTN_parameters[number].Address;
  if ((my_address & 0xF000) == 0x8000) { // Check if this is a MOD block
    if ((uint8_t)(my_address & 0x000F) >= (KTN_fx_memory[current_mod_type].Length)) return true;
  }
  if ((my_address & 0xF000) == 0x9000) { // Check if this is an FX block
    if ((uint8_t)(my_address & 0x007F) >= (KTN_fx_memory[current_fx_type].Length)) return true;
  }

  // Check for empty value in list
  uint16_t fx_index = get_fx_table_index(number);
  if (fx_index != NO_FX_PARAMETER) {
    if (KTN_fx_parameters[fx_index].NumVals == 0) return true;
  }
  return false;
}

// ********************************* Section 6: KTN expression pedal control ********************************************

const PROGMEM uint8_t KTN_FX_control[KTN_NUMBER_OF_FX_TYPES] = { // Which parameters are changed with which effect
  4, // Control FREQ for "TOUCHWAH" (FREQ is the fourth parameter)
  2, // Control FREQ for "AUTO WAH"
  2, // Control PEDAL POS for "SUB WAH"
  0, // Control nothing for "COMPRSOR"
  0, // Control nothing for "LIMITER"
  5, // Control SOLO SW for "DIST"
  0, // Control nothing for "GRAPH EQ"
  0, // Control nothing for "PARAM EQ"
  0, // Control nothing for "TONE MOD"
  0, // Control nothing for "GTR SIM",
  1, // Control SENS for "SLOW GR"
  7, // Control DIR.LVL for "DEFRET"
  7, // Control FX.LVL for "WAV SNTH"
  0, // Control nothing for "SITARSIM"
  2, // Control LEVEL for "OCTAVE"
  0, // Control nothing for "PITCH SH"
  0, // Control nothing for "HARMONST"
  1, // Control HOLD for "SND HOLD"
  0, // Control nothing for "AC. PROC"
  3, // Control DEPTH for "PHASER",
  2, // Control DEPTH for "FLANGER"
  3, // Control DEPTH for "TREMOLO"
  1, // Control RATE for "ROTARY 1"
  2, // Control DEPTH for "UNI-V"
  2, // Control POS for "PAN"
  2, // Control RATE for "SLICER"
  2, // Control DEPTH for "VIBRATO"
  3, // Control FX LVL for "RING MOD"
  6, // Control DEPTH for "HUMANIZR"
  7, // Control H.DEPTH for "2x2 CHOR",
  6, // Control FX.LVL for "SUB DLY"
  0, // Control nothing for "AcGtrSim"
  2, // Control SPEED SW. for "ROTARY 2"
  7, // Control HOLD for "TERA ECHO"
  3, // Control UPPER for "OVERTONE"
  2, // Control SPEED for "PHAS 90E"
  1, // Control MANUAL for "Flgr117E",
};

void MD_KTN_class::move_expression_pedal(uint8_t sw, uint8_t value, uint8_t exp_pedal) {
  uint8_t par_index;
  uint16_t index;
  uint8_t max = 0;
  if (exp_pedal == 0) exp_pedal = current_exp_pedal;
  switch (exp_pedal) {
    case 1:
      par_index = KTN_FOOT_VOL_PARAMETER;
      max = 100;
      break;
    case 2:
      if (KTN_FX_control[current_mod_type] > 0) {
        par_index = KTN_MOD_TYPE_PARAMETER + KTN_FX_control[current_mod_type];
        index = KTN_first_fx_item[current_mod_type] + KTN_FX_control[current_mod_type] - 1;
        max = KTN_fx_parameters[index].NumVals - 1;
      }
      else {
        par_index = KTN_MOD_SW_PARAMETER;
        max = 1;
      }
      break;
    case 3:
      if (KTN_FX_control[current_fx_type] > 0) {
        par_index = KTN_FX_TYPE_PARAMETER + KTN_FX_control[current_fx_type];
        index = KTN_first_fx_item[current_fx_type] + KTN_FX_control[current_fx_type] - 1;
        max = KTN_fx_parameters[index].NumVals - 1;
      }
      else {
        par_index = KTN_FX_SW_PARAMETER;
        max = 1;
      }
      break;
    default:
      return;
  }
  LCD_show_bar(0, value); // Show it on the main display
  uint8_t new_value = map(value, 0, 127, 0, max);
  write_sysex(parameter_address(par_index), new_value);
  //check_update_label(sw, value);
  //request_exp_pedal(sw, exp_pedal);
  LCD_show_popup_label(SP[sw].Label, ACTION_TIMER_LENGTH);
  update_page = REFRESH_FX_ONLY; // To update the other switch states, we re-load the current page
}

void MD_KTN_class::toggle_expression_pedal(uint8_t sw) {
  //uint8_t value;
  if (current_exp_pedal == 0) return;
  current_exp_pedal++;
  if (current_exp_pedal > 3) current_exp_pedal = 1;
  set_expr_title(sw);
  update_lcd = sw;
  update_page = REFRESH_FX_ONLY;
}

void MD_KTN_class::auto_toggle_exp_pedal(uint8_t parameter, uint8_t value) {
  if (parameter == KTN_MOD_SW_PARAMETER) {
    if ((value == 1) && (KTN_FX_control[current_mod_type] > 0)) current_exp_pedal = 2;
    else current_exp_pedal = 1;
  }
  if (parameter == KTN_FX_SW_PARAMETER) {
    if ((value == 1) && (KTN_FX_control[current_fx_type] > 0)) current_exp_pedal = 3;
    else current_exp_pedal = 1;
  }
}

void MD_KTN_class::set_expr_title(uint8_t sw) {
  const char KTN_Exp0[] = " FV  MOD  FX ";
  const char KTN_Exp1[] = "[FV] MOD  FX ";
  const char KTN_Exp2[] = " FV [MOD] FX ";
  const char KTN_Exp3[] = " FV  MOD [FX]";
  if (current_exp_pedal == 1) LCD_set_SP_title(sw, KTN_Exp1);
  else if (current_exp_pedal == 2) LCD_set_SP_title(sw, KTN_Exp2);
  else if (current_exp_pedal == 3) LCD_set_SP_title(sw, KTN_Exp3);
  else LCD_set_SP_title(sw, KTN_Exp0);
}

bool MD_KTN_class::request_exp_pedal(uint8_t sw, uint8_t exp_pedal) {
  uint16_t index;
  String msg = "";

  if (exp_pedal == 0) exp_pedal = current_exp_pedal;

  if (exp_pedal == 0) {
    LCD_clear_SP_label(sw);
    SP[sw].Colour = 0;
  }
  if (exp_pedal == 1) {
    LCD_set_SP_label(sw, KTN_parameters[KTN_FOOT_VOL_PARAMETER].Name);
    SP[sw].Colour = my_LED_colour;
  }
  if (exp_pedal == 2) {
    if (KTN_FX_control[current_mod_type] > 0) {
      index = KTN_first_fx_item[current_mod_type] + KTN_FX_control[current_mod_type] - 1;
      if (!mod_enabled) msg += "(";
      msg += KTN_sublists[22 + current_mod_type];
      msg += " ";
      msg += KTN_fx_parameters[index].Name;
      if (!mod_enabled) msg += ")";
      LCD_set_SP_label(sw, msg);
    }
    else {
      SP[sw].PP_number = KTN_MOD_SW_PARAMETER;
      return request_parameter(sw, KTN_MOD_SW_PARAMETER);
    }
    SP[sw].Colour = KTN_FX_colours[current_mod_type];
  }
  if (exp_pedal == 3) {
    if (KTN_FX_control[current_fx_type] > 0) {
      index = KTN_first_fx_item[current_fx_type] + KTN_FX_control[current_fx_type] - 1;
      if (!fx_enabled) msg += "(";
      msg += KTN_sublists[22 + current_fx_type];
      msg += " ";
      msg += KTN_fx_parameters[index].Name;
      if (!fx_enabled) msg += ")";
      LCD_set_SP_label(sw, msg);
    }
    else {
      SP[sw].PP_number = KTN_FX_SW_PARAMETER;
      return request_parameter(sw, KTN_FX_SW_PARAMETER);
    }
    SP[sw].Colour = KTN_FX_colours[current_fx_type];
  }
  return true;
}