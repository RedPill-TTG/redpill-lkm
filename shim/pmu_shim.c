#define SHIM_NAME "PMU emulator"

#include "pmu_shim.h"
#include "shim_base.h"
#include "../common.h"
#include "../internal/uart/virtual_uart.h"
#include <linux/kfifo.h> //kfifo_*

#define PMU_TTYS_LINE 1 //so far this is hardcoded by syno, so we doubt it will ever change
#define WORK_BUFFER_LEN VUART_FIFO_LEN
#define to_hex_buf_len(len) ((len)*3+1) //2 chars for each hex + space + NULL terminator
#define HEX_BUFFER_LEN to_hex_buf_len(VUART_FIFO_LEN)

//PMU packets are at minimum 2 bytes long (PMU_CMD_HEAD + 1-3 bytes command + optional data). If this is set to a high
// value (e.g. VUART_FIFO_LEN) in practice commands will only be delivered when the client indicates end-of-transmission)
// which may not be bad...
#define PMU_MIN_PACKET 2
#define PMU_CMD_HEAD 0x2d //every PMU packet is delimited by containing 0x2d (ASCII "-"/dash) as its first character

typedef struct command_definition command_definition;

/**
 * A single PMU command and its routing
 */
struct command_definition {
    void (*fn) (const command_definition *t, const char *data, u8 data_len);
    const u8 length; //commands are realistically 1-3 chars only
    const char *name;
} __packed;

/**
 * Result for matching of command signature against known list
 */
typedef enum {
    PMU_CMD_AMBIGUOUS = -1,
    PMU_CMD_NOT_FOUND =  0,
    PMU_CMD_FOUND     =  1,
} pmu_match_status;

/**
 * Default/noop shim for a PMU command. It simply prints the command received.
 */
static void cmd_shim_noop(const command_definition *t, const char *data, u8 data_len)
{
    pr_loc_dbg("vPMU received %s using %d bytes - NOOP", t->name, data_len);
}

//@todo when we get the physical PMU emulator we can move this to a separate library so that shim contacts an internal
// routing routine for commands which aren't shimmed here. Then we will add all PMU=>kernel commands as well. Currently
// we only define kernel=>PMU ones as these are the ones we need to listen for.
#define PMU_CMD__MIN_CODE 0x30
#define PMU_CMD__MAX_CODE 0x75
#define single_byte_idx(id) ((id)-PMU_CMD__MIN_CODE)
#define get_single_byte_cmd(id) single_byte_cmds[single_byte_idx(id)] //call it ONLY after has_single_byte_cmd!!!
#define has_single_byte_cmd(id) \
    (likely((id) >= PMU_CMD__MIN_CODE) && likely((id) <= PMU_CMD__MAX_CODE) && get_single_byte_cmd(id).length != 0)
#define DEFINE_SINGLE_BYTE_CMD(cnm, fp) [single_byte_idx(PMU_CMD_ ## cnm)] = { .name = #cnm, .length = 1, .fn = fp }

#define PMU_CMD_OUT_HW_POWER_OFF 0x31 //"1"
#define PMU_CMD_OUT_BUZ_SHORT 0x32 //"2"
#define PMU_CMD_OUT_BUZ_LONG 0x33 //"3"
#define PMU_CMD_OUT_PWR_LED_ON 0x34 //"4"
#define PMU_CMD_OUT_PWR_LED_BLINK 0x35 //"5"
#define PMU_CMD_OUT_PWR_LED_OFF 0x36 //"6"
#define PMU_CMD_OUT_STATUS_LED_OFF 0x37 //"7"
#define PMU_CMD_OUT_STATUS_LED_ON_GREEN 0x38 //"8"
#define PMU_CMD_OUT_STATUS_LED_PULSE_GREEN 0x39 //"9"
#define PMU_CMD_OUT_STATUS_LED_ON_ORANGE 0x3A //":"
#define PMU_CMD_OUT_STATUS_LED_PULSE_ORANGE 0x3B //";"
//0x3C unknown (possibly not used)
#define PMU_CMD_OUT_STATUS_LED_PULSE 0x3d //"="
//0x3E-3F unknown (possibly not used)
#define PMU_CMD_OUT_USB_LED_ON 0x40 //"@"
#define PMU_CMD_OUT_USB_LED_PULSE 0x41 //"A"
#define PMU_CMD_OUT_USB_LED_OFF 0x42 //"B"
#define PMU_CMD_OUT_HW_RESET 0x43 //"C"
//0x43-4A unknown
#define PMU_CMD_OUT_10G_LED_ON 0x4a //"J"
#define PMU_CMD_OUT_10G_LED_OFF 0x4b //"K"
//0x4C unknown
#define PMU_CMD_OUT_LED_TOG_PWR_STAT 0x4d //"M", allows for using one led for status and power and toggle between them
//0x4E unknown
#define PMU_CMD_OUT_SWITCH_UP_VER 0x4f //"O"
#define PMU_CMD_OUT_MIR_LED_OFF 0x50 //"P"
//0x51-55 unknown (except 52)
#define PMU_CMD_OUT_GET_UNIQ 0x52 //"P"
#define PMU_CMD_OUT_PWM_CYCLE 0x56 //"V"
#define PMU_CMD_OUT_PWM_HZ 0x57 //"W"
//0x58-59 unknown
//0x60-71 inputs (except 6C)
#define PMU_CMD_OUT_WOL_ON 0x6c //"l"
#define PMU_CMD_OUT_SCHED_UP_OFF 0x72 //"r"
#define PMU_CMD_OUT_SCHED_UP_ON 0x73 //"s"
#define PMU_CMD_OUT_FAN_HEALTH_OFF 0x74 //"t"
#define PMU_CMD_OUT_FAN_HEALTH_ON 0x75 //"u"

static const command_definition single_byte_cmds[single_byte_idx(PMU_CMD__MAX_CODE)+1] = {
    DEFINE_SINGLE_BYTE_CMD(OUT_HW_POWER_OFF, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_BUZ_SHORT, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_BUZ_LONG, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_PWR_LED_ON, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_PWR_LED_BLINK, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_PWR_LED_OFF, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_STATUS_LED_OFF, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_STATUS_LED_ON_GREEN, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_STATUS_LED_PULSE_GREEN, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_STATUS_LED_ON_ORANGE, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_STATUS_LED_PULSE_ORANGE, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_STATUS_LED_PULSE, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_USB_LED_ON, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_USB_LED_PULSE, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_USB_LED_OFF, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_HW_RESET, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_10G_LED_ON, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_10G_LED_OFF, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_LED_TOG_PWR_STAT, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_SWITCH_UP_VER, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_MIR_LED_OFF, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_GET_UNIQ, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_PWM_CYCLE, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_PWM_HZ, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_WOL_ON, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_SCHED_UP_OFF, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_SCHED_UP_ON, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_FAN_HEALTH_OFF, cmd_shim_noop),
    DEFINE_SINGLE_BYTE_CMD(OUT_FAN_HEALTH_ON, cmd_shim_noop),
};


static char *uart_buffer = NULL; //keeps data streamed directly by the vUART... todo: vUART should manage this buffer
static char *work_buffer = NULL; //collecting & operatint on the data received from vUART
static char *work_buffer_curr = NULL; //pointer to the current free space in work_buffer
static char *hex_print_buffer = NULL; //helper buffer to print char arrays in hex

#define work_buffer_fill() ((unsigned int)(work_buffer_curr - work_buffer))

/**
 * Free all buffers used by this submodule
 *
 * It is safe to call this method without checking buffers state (it has a deliberate protection against double-free)
 */
static void free_buffers(void)
{
    if (likely(uart_buffer))
        kfree(uart_buffer);

    if (likely(work_buffer))
        kfree(work_buffer);

    if (likely(hex_print_buffer))
        kfree(hex_print_buffer);

    uart_buffer = NULL;
    work_buffer = NULL;
    work_buffer_curr = NULL;
    hex_print_buffer = NULL;
}

/**
 * Allocates space for all buffers used by this submodule
 */
static int alloc_buffers(void)
{
    kmalloc_or_exit_int(uart_buffer, VUART_FIFO_LEN);
    kmalloc_or_exit_int(work_buffer, WORK_BUFFER_LEN);
    kmalloc_or_exit_int(hex_print_buffer, HEX_BUFFER_LEN);

    work_buffer_curr = work_buffer;

    return 0;
}

/**
 * Converts passed char buffer into user-readable hex print of it
 *
 * @todo this should probably be extracted
 */
static __used const char *get_hex_print(const char *buffer, int len)
{
    if (unlikely(len == 0)) {
        hex_print_buffer[0] = '\0';
        return hex_print_buffer;
    } else if (unlikely(to_hex_buf_len(len) > HEX_BUFFER_LEN)) {
        pr_loc_bug("Printing %d bytes as hex requires %d bytes in buffer - buffer is %d bytes", len,
                   to_hex_buf_len(len), HEX_BUFFER_LEN);
        hex_print_buffer[0] = '\0';
        return hex_print_buffer;
    }

    int hex_len = 0;
    for (int i = 0; i < len; ++i) {
        sprintf(&hex_print_buffer[i * 3], "%02x ", buffer[i]);
        hex_len += 3;
    }

    hex_print_buffer[hex_len-1] = '\0';
    return hex_print_buffer;
}

/**
 * Matches command against a list of known ones based on the signature specified
 *
 * @param cmd pointer to a pointer where address of command structure can be saved if found
 */
static pmu_match_status noinline
match_command(const command_definition **cmd, const char *signature, const unsigned int sig_len)
{
    if (unlikely(sig_len == 0)) {
        pr_loc_dbg("Invalid zero-length command (stray head without command signature) - discarding");
        return PMU_CMD_NOT_FOUND;
    }

    if (likely(sig_len == 1) //regular 1 byte
        || (sig_len == 3 && signature[1] == 0x0d && signature[2] == 0x0a) //1 byte with CRLF (sic!)
       ) {
        if (!has_single_byte_cmd(signature[0]))
            return PMU_CMD_NOT_FOUND;

        *cmd = &get_single_byte_cmd(signature[0]);
        return PMU_CMD_FOUND;
    }

    return PMU_CMD_NOT_FOUND; //@todo Currently we don't handle multibyte commands; it has to be a full iteration
}

/**
 * Finds command based on its signature and execute its callback if found
 */
static void route_command(const char *buffer, const unsigned int len)
{
    const command_definition *cmd = NULL;

    if (match_command(&cmd, buffer, len) != PMU_CMD_FOUND) {
        pr_loc_wrn("Unknown %d byte PMU command with signature hex=\"%s\" ascii=\"%.*s\"", len,
                   get_hex_print(buffer, len), len, buffer);
        return;
    }

    pr_loc_dbg("Executing cmd %s handler %pF", cmd->name, cmd->fn);
    cmd->fn(cmd, buffer, len);
}

/**
 * Scans work buffer (copied from vUART buffer) to find commands
 *
 * @param end_of_packet Indicates whether this command was called because the vUART transmitter assumed
 *                      end-of-transmission/IDLE, or flushed due to its buffer being full. If this parameter is true the
 *                      data in the work buffer is assumed to be a complete representation of the state. This becomes
 *                      important if we cannot with 100% confidence say, after reaching the end of the buffer, if it
 *                      will be a multibyte command (but we possibly didn't get all the bytes YET) or this is single or
 *                      multibyte command which we don't know.
 *
 * This function makes an assumption that when it's called that the buffer represents the whole state of the command
 * If this becomes to take too long we can move it to a separate thread, but this will require a lock.
 */
static noinline void process_work_buffer(bool end_of_packet)
{
    if (unlikely(work_buffer == work_buffer_curr)) {
        //this can happen if kernel sends no data but we get IDLE... shouldn't logically happen
        pr_loc_wrn("%s called on empty buffer?!", __FUNCTION__);
        return;
    }

    int cmd_len = -1; //number of bytes in the command (excluding header)
    for(char *curr = work_buffer; curr < work_buffer_curr; ++curr) {
        if (*curr == PMU_CMD_HEAD) { //got the beginning of a new command
            //we've found a new command in the buffer - lets check if the previously collected data matches anything
            if (cmd_len != -1) { //we only want to call it if this isn't the first byte after last cmd (or 1st in buf)
                route_command(curr-cmd_len, cmd_len);
                cmd_len = 0; //we've got the head so 0 and not -1
            } else {
                ++cmd_len; //We've read the buffer containing head, we then expect to get something which is non-head
            }
        } else {
            if (cmd_len == -1) { //we don't expect data before head
                pr_loc_wrn("Found garbage data in PMU buffer before cmd head (\"%c\" / 0x%02x) - ignoring", *curr,
                           *curr);
                continue;
            }

            ++cmd_len; //collecting another byte for the currently processed command
        }
    }

    //We've finished processing the buffer. Now we need to decide what to do with that last piece of data
    unsigned int processed = work_buffer_fill();
    if (cmd_len != -1) { //if it's -1 it means we didn't find any heading so we're just discarding all data
        if (end_of_packet) {
            route_command(work_buffer_curr-cmd_len, cmd_len);
        } else { //if the packed didn't end we need to keep that piece of buffer for the next run
            processed -= cmd_len + 1; //we also keep head
        }
    }

    unsigned int left = work_buffer_fill() - processed;
    if (likely(left != 0)) {
        memmove(work_buffer, work_buffer+processed, left);
    }
    work_buffer_curr = work_buffer + left;

//    pr_loc_dbg("Left buffer %p curr=%p with %d bytes in it (ascii=\"%.*s\" hex={%s})", work_buffer, work_buffer_curr, left, left, work_buffer,
//               get_hex_print(work_buffer, left));
}

/**
 * Callback passed to vUART. It will be called any time some data is available.
 */
static noinline void pmu_rx_callback(int line, const char *buffer, unsigned int len, vuart_flush_reason reason)
{
    pr_loc_dbg("Got %d bytes from PMU: reason=%d hex={%s} ascii=\"%.*s\"", len, reason, get_hex_print(buffer, len), len, buffer);

    int buffer_space = WORK_BUFFER_LEN - work_buffer_fill();
    if (unlikely(work_buffer_curr + len > work_buffer + WORK_BUFFER_LEN)) { //todo just remove as much as needed from the buffer to fit more data
        pr_loc_err("Work buffer is full! Only %d of %d bytes will be copied from receiver", len, buffer_space);
        len = buffer_space;
    }

    memcpy(work_buffer_curr, buffer, len);
    work_buffer_curr += len;
//    pr_loc_dbg("Copied data to work buffer, now with %d bytes in it (cur=%p)",
//               (unsigned int)(work_buffer_curr - work_buffer), work_buffer_curr);

    //We only want to analyze the buffer when we are sure we have the full command to process. This is because commands
    // are variable length and have no end delimiter not length specified with prefixes of short commands conflicting
    // with longer commands (sic!)
    //For example, you have "SW1" command which when sent will look like "-SW1" (0x2d 0x53 0x57 0x31). We can capture
    // this when VUART_FLUSH_IDLE happens. We can also easily capture this when multiple commands are sent at once
    // (unlikely but possible) since it will be something like "-SW1-3". However, we CANNOT distinguish "-S" from
    // incomplete "-SW1". So we need to rely on IDLE - if we got "-S" with IDLE it means it was "-S" and not the
    // beginning of "-SW1".
    //We also forcefully flush on full buffer even if no IDLE was specified, as it's technically possible for the
    // software to send a long sequence of commands at once totaling more than our buffer (extremely unlikely).
    //Additionally, we only process IDLE-signalled buffers when they have at least a single byte of data as some
    // versions of the mfgBIOS attach head AND THEN in a separate packet send the actual commands (sic!)
    if (reason == VUART_FLUSH_IDLE && work_buffer_fill() > 1)
        process_work_buffer(true);
    //our buffer is full [we must process] or vUART buffer was full [we should process]
    else if (buffer_space <= len || reason == VUART_FLUSH_FULL)
        process_work_buffer(false);
}

int register_pmu_shim(const struct hw_config *hw)
{
    shim_reg_in();

    int out;
    if ((out = vuart_add_device(PMU_TTYS_LINE) != 0)) {
        pr_loc_err("Failed to initialize vUART for PMU at ttyS%d", PMU_TTYS_LINE);
        return out;
    }

    if ((out = alloc_buffers()) != 0)
        goto error_out;

    //We don't set the threshold as some commands are variable length but the "packets" are properly split
    if ((out = vuart_set_tx_callback(PMU_TTYS_LINE, pmu_rx_callback, uart_buffer, VUART_THRESHOLD_MAX))) {
        pr_loc_err("Failed to register RX callback");
        goto error_out;
    }

    shim_reg_ok();
    return 0;

    error_out:
    free_buffers();
    vuart_remove_device(PMU_TTYS_LINE); //this also removes callback (if set)
    return out;
}

int unregister_pmu_shim(void)
{
    shim_ureg_in();

    int out = 0;
    if (unlikely(!uart_buffer)) {
        pr_loc_bug("Attempted to %s while it's not registered", __FUNCTION__);
        return 0; //Technically it succeeded
    }

    if ((out = vuart_remove_device(PMU_TTYS_LINE)) != 0)
        pr_loc_err("Failed to remove vUART for line=%d", PMU_TTYS_LINE);

    free_buffers();

    shim_ureg_ok();
    return out;
}