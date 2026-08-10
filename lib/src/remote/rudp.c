#include <chiaki/remote/rudp.h>
#include <chiaki/random.h>
#include <chiaki/thread.h>
#include <chiaki/remote/rudpsendbuffer.h>
#include <chiaki/time.h>

#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define RUDP_CONSTANT 0x244F244F
#define RUDP_SEND_BUFFER_SIZE 16
#define RUDP_EXPECT_TIMEOUT_MS 1000
// Upper bound on the number of benign (non-matching) chunks chiaki_rudp_send_recv() will silently
// drain in a single try before giving up and treating it like a timeout. Protects against a chatty
// peer (e.g. a stream of selective-ack/gap-report chunks) spinning us forever without ever consuming
// a retry budget. See the RudpPacketType enum comment in rudp.h for the underlying finding.
#define RUDP_MAX_DRAIN_MESSAGES 32
typedef struct rudp_t
{
    uint16_t counter;
    uint32_t header;
    ChiakiMutex counter_mutex;
    ChiakiStopPipe stop_pipe;
    chiaki_socket_t sock;
    ChiakiLog *log;
    ChiakiRudpSendBuffer send_buffer;
} RudpInstance;

/** Outcome of a single receive-and-classify attempt inside chiaki_rudp_send_recv(). */
typedef enum rudp_recv_outcome_t
{
    RUDP_RECV_MATCH,     // Message matches the requested recv_type and satisfies min_data_size.
    RUDP_RECV_DRAIN,     // Message is unrelated (or benign peer control traffic) -- keep waiting, no resend.
    RUDP_RECV_TOO_SMALL, // Message matches recv_type but is smaller than min_data_size -- resend.
    RUDP_RECV_TIMEOUT,   // chiaki_rudp_select_recv() timed out -- resend.
    RUDP_RECV_ERROR,     // Fatal error (socket/memory); see *out_err.
} RudpRecvOutcome;

static uint16_t get_then_increase_counter(RudpInstance *rudp);
static ChiakiErrorCode chiaki_rudp_message_parse(uint8_t *serialized_msg, size_t msg_size, RudpMessage *message);
static void rudp_message_serialize(RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size);
static void print_rudp_message_type(RudpInstance *rudp, RudpPacketType type);
static bool assign_submessage_to_message(RudpMessage *message);
static ChiakiErrorCode rudp_send_init_message_with_counter(RudpInstance *rudp, uint16_t local_counter);
static ChiakiErrorCode rudp_send_cookie_message_with_counter(RudpInstance *rudp, uint16_t local_counter, uint8_t *response_buf, size_t response_size);
static ChiakiErrorCode rudp_send_session_message_with_counter(RudpInstance *rudp, uint16_t local_counter, uint16_t remote_counter, uint8_t *session_msg, size_t session_msg_size);
static bool rudp_subtype_is_benign_control(uint8_t subtype);
static const char *rudp_benign_subtype_name(uint8_t subtype);
static void rudp_log_received_chunk(RudpInstance *rudp, RudpMessage *message);
static RudpRecvOutcome rudp_recv_and_classify(RudpInstance *rudp, RudpMessage *message, RudpPacketType recv_type, size_t min_data_size, ChiakiErrorCode *out_err);


CHIAKI_EXPORT RudpInstance *chiaki_rudp_init(chiaki_socket_t *sock, ChiakiLog *log)
{
    RudpInstance *rudp = (RudpInstance *)calloc(1, sizeof(RudpInstance));
    if(!rudp)
        return NULL;
    rudp->log = log;
    ChiakiErrorCode err;
    err = chiaki_mutex_init(&rudp->counter_mutex, false);
    assert(err == CHIAKI_ERR_SUCCESS);
    err = chiaki_stop_pipe_init(&rudp->stop_pipe);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(rudp->log, "Rudp failed initializing, failed creating stop pipe");
        return NULL;
    }
    chiaki_rudp_reset_counter_header(rudp);
    rudp->sock = *sock;
	// The send buffer size MUST be consistent with the acked seqnums array size in rudp_handle_message_ack()
    err = chiaki_rudp_send_buffer_init(&rudp->send_buffer, rudp, log, RUDP_SEND_BUFFER_SIZE);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(rudp->log, "Rudp failed initializing, failed creating send buffer");
        return NULL;
    }
    return rudp;
}

CHIAKI_EXPORT void chiaki_rudp_reset_counter_header(RudpInstance *rudp)
{
    chiaki_mutex_lock(&rudp->counter_mutex);
    rudp->counter = chiaki_random_32()%0x5E00 + 0x1FF;
    chiaki_mutex_unlock(&rudp->counter_mutex);
    rudp->header = chiaki_random_32() + 0x8000;
}

/**
 * Serializes and sends an init rudp message using an explicit local counter.
 *
 * Factored out of chiaki_rudp_send_init_message() so that chiaki_rudp_send_recv() can resend the
 * identical logical request (same counter) on retry instead of minting a new counter every attempt,
 * which otherwise leaves the peer's selective-ack window permanently stuck (see Fix 2 in the
 * session-request hardening notes).
 *
 * @param[in] rudp The rudp instance to use
 * @param[in] local_counter The counter value to stamp this message with
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 */
static ChiakiErrorCode rudp_send_init_message_with_counter(RudpInstance *rudp, uint16_t local_counter)
{
    RudpMessage message;
    message.type = INIT_REQUEST;
    message.subMessage = NULL;
    message.data_size = 14;
    uint8_t data[message.data_size];
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    const uint8_t after_header[0x2] = { 0x05, 0x82 };
    const uint8_t after_counter[0x6] = { 0x0B, 0x01, 0x01, 0x00, 0x01, 0x00 };
    *(chiaki_unaligned_uint16_t *)(data) = htons(local_counter);
    memcpy(data + 2, after_counter, sizeof(after_counter));
    *(chiaki_unaligned_uint32_t *)(data + 8) = htonl(rudp->header);
    memcpy(data + 12, after_header, sizeof(after_header));
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_init_message(RudpInstance *rudp)
{
    return rudp_send_init_message_with_counter(rudp, get_then_increase_counter(rudp));
}

/**
 * Serializes and sends a cookie rudp message using an explicit local counter.
 *
 * See rudp_send_init_message_with_counter() for why this is factored out from the exported
 * chiaki_rudp_send_cookie_message() -- it lets chiaki_rudp_send_recv() reuse the same counter on retry.
 *
 * @param[in] rudp The rudp instance to use
 * @param[in] local_counter The counter value to stamp this message with
 * @param[in] response_buf The response from the init message
 * @param[in] response_size The size of the response from the init message
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 */
static ChiakiErrorCode rudp_send_cookie_message_with_counter(RudpInstance *rudp, uint16_t local_counter, uint8_t *response_buf, size_t response_size)
{
    RudpMessage message;
    message.type = COOKIE_REQUEST;
    message.subMessage = NULL;
    message.data_size = 14 + response_size;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t data[message.data_size];
    const uint8_t after_header[0x2] = { 0x05, 0x82 };
    const uint8_t after_counter[0x6] = { 0x0B, 0x01, 0x01, 0x00, 0x01, 0x00 };
    *(chiaki_unaligned_uint16_t *)(data) = htons(local_counter);
    memcpy(data + 2, after_counter, sizeof(after_counter));
    *(chiaki_unaligned_uint32_t *)(data + 8) = htonl(rudp->header);
    memcpy(data + 12, after_header, sizeof(after_header));
    memcpy(data + 14, response_buf, response_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_cookie_message(RudpInstance *rudp, uint8_t *response_buf, size_t response_size)
{
    return rudp_send_cookie_message_with_counter(rudp, get_then_increase_counter(rudp), response_buf, response_size);
}

/**
 * Serializes and sends a session rudp message using an explicit local counter.
 *
 * See rudp_send_init_message_with_counter() for why this is factored out from the exported
 * chiaki_rudp_send_session_message() -- it lets chiaki_rudp_send_recv() reuse the same counter on
 * retry so the peer's selective-ack window can actually advance instead of chasing a moving target.
 *
 * @param[in] rudp The rudp instance to use
 * @param[in] local_counter The counter value to stamp this message with
 * @param[in] remote_counter The remote counter of the message this message is being sent in response to
 * @param[in] session_msg The data from the session msg (i.e., regist message)
 * @param[in] session_msg_size The size of the session message
 * @return CHIAKI_ERR_SUCCESS on success, otherwise another error code
 */
static ChiakiErrorCode rudp_send_session_message_with_counter(RudpInstance *rudp, uint16_t local_counter, uint16_t remote_counter, uint8_t *session_msg, size_t session_msg_size)
{
    RudpMessage subMessage;
    subMessage.type = CTRL_MESSAGE;
    subMessage.subMessage = NULL;
    subMessage.data_size = 2 + session_msg_size;
    subMessage.size = (0xC << 12) | (8 + subMessage.data_size);
    uint8_t subdata[subMessage.data_size];
    *(chiaki_unaligned_uint16_t *)(subdata) = htons(local_counter);
    memcpy(subdata + 2, session_msg, session_msg_size);
    subMessage.data = subdata;

    RudpMessage message;
    message.type = SESSION_MESSAGE;
    message.subMessage = &subMessage;
    message.data_size = 4;
    size_t alloc_size = 8 + message.data_size + 8 + subMessage.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | (8 + message.data_size);
    uint8_t data[message.data_size];
    *(chiaki_unaligned_uint16_t *)(data) = htons(local_counter);
    *(chiaki_unaligned_uint16_t *)(data + 2) = htons(remote_counter);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_session_message(RudpInstance *rudp, uint16_t remote_counter, uint8_t *session_msg, size_t session_msg_size)
{
    return rudp_send_session_message_with_counter(rudp, get_then_increase_counter(rudp), remote_counter, session_msg, session_msg_size);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_ack_message(RudpInstance *rudp, uint16_t remote_counter)
{
    RudpMessage message;
    uint16_t counter = rudp->counter;
    message.type = ACK;
    message.subMessage = NULL;
    message.data_size = 6;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t data[message.data_size];
    const uint8_t after_counters[0x2] = { 0x00, 0x92 };
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    *(chiaki_unaligned_uint16_t *)(data + 2) = htons(remote_counter);
    memcpy(data + 4, after_counters, sizeof(after_counters));
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    free(serialized_msg);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_ctrl_message(RudpInstance *rudp, uint8_t *ctrl_message, size_t ctrl_message_size)
{
    RudpMessage message;
    uint16_t counter = get_then_increase_counter(rudp);
    uint16_t counter_ack = rudp->counter;
    message.type = CTRL_MESSAGE;
    message.subMessage = NULL;
    message.data_size = 2 + ctrl_message_size;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t data[message.data_size];
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    memcpy(data + 2, ctrl_message, ctrl_message_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, counter_ack, serialized_msg, msg_size);
    return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_switch_to_stream_connection_message(RudpInstance *rudp)
{
    RudpMessage message;
    uint16_t counter = get_then_increase_counter(rudp);
    uint16_t counter_ack = rudp->counter;
    message.type = CTRL_MESSAGE;
    message.subMessage = NULL;
    message.data_size = 26;
    size_t alloc_size = 8 + message.data_size;
    uint8_t *serialized_msg = malloc(alloc_size * sizeof(uint8_t));
    if(!serialized_msg)
    {
        CHIAKI_LOGE(rudp->log, "Error allocating memory for rudp message");
        return CHIAKI_ERR_MEMORY;
    }
    size_t msg_size = 0;
    message.size = (0xC << 12) | alloc_size;
    uint8_t data[message.data_size];
    const size_t buf_size = 16;
    uint8_t buf[buf_size];
    const uint8_t before_buf[8] = { 0x00, 0x00, 0x00, 0x10, 0x00, 0x0D, 0x00, 0x00 };
    chiaki_random_bytes_crypt(buf, buf_size);
    *(chiaki_unaligned_uint16_t *)(data) = htons(counter);
    memcpy(data + 2, before_buf, sizeof(before_buf));
    memcpy(data + 10, buf, buf_size);
    message.data = data;
    rudp_message_serialize(&message, serialized_msg, &msg_size);
    ChiakiErrorCode err = chiaki_rudp_send_raw(rudp, serialized_msg, msg_size);
    if(err != CHIAKI_ERR_SUCCESS)
    {
        free(serialized_msg);
        return err;
    }
    err = chiaki_rudp_send_buffer_push(&rudp->send_buffer, counter_ack, serialized_msg, msg_size);
    return err;
}

/**
 * Serializes rudp message into byte array
 *
 * @param[in] message The rudp message to serialize
 * @param[out] serialized_msg The serialized message
 * @param[out] msg_size The size of the serialized message
 * 
*/
static void rudp_message_serialize(
    RudpMessage *message, uint8_t *serialized_msg, size_t *msg_size)
{
    *(chiaki_unaligned_uint16_t *)(serialized_msg) = htons(message->size);
    *(chiaki_unaligned_uint32_t *)(serialized_msg + 2) = htonl(RUDP_CONSTANT);
    *(chiaki_unaligned_uint16_t *)(serialized_msg + 6) = htons(message->type);
    memcpy(serialized_msg + 8, message->data, message->data_size);
    *msg_size += 8 + message->data_size;
    if(message->subMessage)
    {
        rudp_message_serialize(message->subMessage, serialized_msg + 8 + message->data_size, msg_size);
    }
}

/**
 * Parse serialized message into rudp message
 *
 * @param[in] serialized_msg The serialized message to transform to a rudp message
 * @param[in] msg_size The size of the serialized message
 * @param[out] RudpMessage The parsed rudp message
 * @return CHIAKI_ERR_SUCCESS on sucess or error code on failure
 * 
*/
static ChiakiErrorCode chiaki_rudp_message_parse(
    uint8_t *serialized_msg, size_t msg_size, RudpMessage *message)
{
    ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
    message->data = NULL;
    message->subMessage = NULL;
    message->subMessage_size = 0;
    message->data_size = 0;
    message->size = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg));
    message->type = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg + 6));
    message->subtype = serialized_msg[6] & 0xFF;
    // Eliminate 0xC before length (size of header + data but not submessage)
    serialized_msg[0] = serialized_msg[0] & 0x0F;
    message->remote_counter = 0;
    uint16_t length = ntohs(*(chiaki_unaligned_uint16_t *)(serialized_msg));
    int remaining = msg_size - 8;
    int data_size = 0;
    if(length > 8)
    {
        data_size = length - 8;
        if(remaining < data_size)
            data_size = remaining;
        message->data_size = data_size;
        message->data = malloc(message->data_size * sizeof(uint8_t));
        if(!message->data)
            return CHIAKI_ERR_MEMORY;
        memcpy(message->data, serialized_msg + 8, data_size);
        if(data_size >= 2)
            message->remote_counter = ntohs(*(chiaki_unaligned_uint16_t *)(message->data)) + 1;
    }

    remaining = remaining - data_size;
    if (remaining >= 8)
    {
        message->subMessage = malloc(1 * sizeof(RudpMessage));
        if(!message->subMessage)
            return CHIAKI_ERR_MEMORY;
        message->subMessage_size = remaining;
        err = chiaki_rudp_message_parse(serialized_msg + 8 + data_size, remaining, message->subMessage);
    }
    return err;
}

/**
 * Get current rudp local counter and then increase rudp local counter
 *
 * @param[in] rudp The rudp instance to use
 * @return The rudp counter before increasing
 * 
*/
static uint16_t get_then_increase_counter(RudpInstance *rudp)
{
    chiaki_mutex_lock(&rudp->counter_mutex);
    uint16_t tmp = rudp->counter;
    if(rudp->counter >= UINT16_MAX)
        rudp->counter = 0;
    else
        rudp->counter++;
    chiaki_mutex_unlock(&rudp->counter_mutex);
    return tmp;
}

CHIAKI_EXPORT uint16_t chiaki_rudp_get_local_counter(RudpInstance *rudp)
{
    return rudp->counter;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_raw(RudpInstance *rudp, uint8_t *buf, size_t buf_size)
{
    if(rudp->sock < 0)
    {
        return CHIAKI_ERR_DISCONNECTED;
    }
    CHIAKI_LOGV(rudp->log, "Sending Message:");
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_VERBOSE, buf, buf_size);
	int sent = send(rudp->sock, (CHIAKI_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(sent < 0)
	{
		CHIAKI_LOGE(rudp->log, "Rudp raw failed to send packet: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		return CHIAKI_ERR_NETWORK;
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_select_recv(RudpInstance *rudp, size_t buf_size,  RudpMessage *message)
{
    uint8_t buf[buf_size]; 
	ChiakiErrorCode err = chiaki_stop_pipe_select_single(&rudp->stop_pipe, rudp->sock, false, RUDP_EXPECT_TIMEOUT_MS);
	if(err == CHIAKI_ERR_TIMEOUT || err == CHIAKI_ERR_CANCELED)
		return err;
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(rudp->log, "Rudp select failed: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		return err;
	}

	int received_sz = recv(rudp->sock, (CHIAKI_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(received_sz <= 8)
	{
		if(received_sz < 0)
			CHIAKI_LOGE(rudp->log, "Rudp recv failed: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		else
			CHIAKI_LOGE(rudp->log, "Rudp recv returned less than the required 8 byte RUDP header");
		return CHIAKI_ERR_NETWORK;
	}
    CHIAKI_LOGV(rudp->log, "Receiving message:");
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_VERBOSE, buf, received_sz);

    err = chiaki_rudp_message_parse(buf, received_sz, message);
    
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_recv_only(RudpInstance *rudp, size_t buf_size,  RudpMessage *message)
{
    uint8_t buf[buf_size];
	int received_sz = recv(rudp->sock, (CHIAKI_SOCKET_BUF_TYPE) buf, buf_size, 0);
	if(received_sz <= 8)
	{
		if(received_sz < 0)
			CHIAKI_LOGE(rudp->log, "Rudp recv failed: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		else
			CHIAKI_LOGE(rudp->log, "Rudp recv returned less than the required 8 byte RUDP header");
		return CHIAKI_ERR_NETWORK;
	}
    CHIAKI_LOGV(rudp->log, "Receiving message:");
    chiaki_log_hexdump(rudp->log, CHIAKI_LOG_VERBOSE, buf, received_sz);

    ChiakiErrorCode err = chiaki_rudp_message_parse(buf, received_sz, message);

	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_stop_pipe_select_single(RudpInstance *rudp, ChiakiStopPipe *stop_pipe, uint64_t timeout)
{
	ChiakiErrorCode err = chiaki_stop_pipe_select_single(stop_pipe, rudp->sock, false, timeout);
	if(err == CHIAKI_ERR_TIMEOUT || err == CHIAKI_ERR_CANCELED)
		return err;
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(rudp->log, "Rudp select failed: " CHIAKI_SOCKET_ERROR_FMT, CHIAKI_SOCKET_ERROR_VALUE);
		return err;
	}
    return CHIAKI_ERR_SUCCESS;
}

/**
 * Identifies subtypes that are known-benign peer control traffic unrelated to the response a
 * chiaki_rudp_send_recv() caller is waiting for: plain ACKs (subtype 0x24, which also covers the
 * mis-catalogued STREAM_CONNECTION_SWITCH_ACK=0x242E -- same subtype, decrementing low byte) and the
 * session-class selective-ack/gap-report chunks (subtype 0x30, e.g. observed types 0x302F/0x302E/0x302D).
 * See the RudpPacketType enum comment in rudp.h for the full finding.
 *
 * @param[in] subtype The subtype byte of a received RudpMessage
 * @return true if this subtype should be silently drained rather than logged as an anomaly
 */
static bool rudp_subtype_is_benign_control(uint8_t subtype)
{
    return subtype == 0x24 || subtype == 0x30;
}

/**
 * Human-readable name for a benign control subtype, for diagnostic logging.
 *
 * @param[in] subtype The subtype byte of a received RudpMessage
 * @return A short, static description; "control" if the subtype is not one of the known benign ones
 */
static const char *rudp_benign_subtype_name(uint8_t subtype)
{
    switch(subtype)
    {
        case 0x24:
            return "ack";
        case 0x30:
            return "session gap-report";
        default:
            return "control";
    }
}

/**
 * Logs a concise, single-line summary of a just-received rudp chunk for handshake diagnostics:
 * subtype, the peer's raw on-wire counter, and -- when the payload is large enough to carry it -- the
 * peer's ack of our own counter stream. Verbose-only: chiaki_rudp_send_recv() can drain many benign
 * chunks per call, and this runs on Vita hardware.
 *
 * @param[in] rudp The rudp instance to use for logging
 * @param[in] message The just-parsed, not-yet-classified RudpMessage
 */
static void rudp_log_received_chunk(RudpInstance *rudp, RudpMessage *message)
{
    // NOTE: message->remote_counter (set by chiaki_rudp_message_parse()) already stores the raw
    // on-wire counter + 1 -- it's "the counter value we should next ack", not the peer's counter as
    // sent. Recompute the raw value from message->data here so this log isn't misleading.
    // Layout: [peer counter (2B)][peer-ack of our last accepted counter (2B)][...], bounds-checked
    // explicitly rather than inferred from the subtype to avoid ever reading past message->data.
    if(message->data && message->data_size >= 4)
    {
        uint16_t peer_counter = ntohs(*(chiaki_unaligned_uint16_t *)(message->data));
        uint16_t peer_ack = ntohs(*(chiaki_unaligned_uint16_t *)(message->data + 2));
        CHIAKI_LOGV(rudp->log, "Rudp recv: subtype=0x%02x peer_counter=0x%04x peer_ack=0x%04x",
            message->subtype, peer_counter, peer_ack);
    }
    else if(message->data && message->data_size >= 2)
    {
        uint16_t peer_counter = ntohs(*(chiaki_unaligned_uint16_t *)(message->data));
        CHIAKI_LOGV(rudp->log, "Rudp recv: subtype=0x%02x peer_counter=0x%04x", message->subtype, peer_counter);
    }
    else
    {
        CHIAKI_LOGV(rudp->log, "Rudp recv: subtype=0x%02x (no counter, data_size=%zu)", message->subtype, message->data_size);
    }
}

/**
 * Receives one rudp message and classifies it against the recv_type a chiaki_rudp_send_recv() caller
 * is waiting for. Does not send anything and does not loop internally on RUDP_RECV_DRAIN -- the caller
 * drives the drain loop so it can enforce the shared deadline/max-drain budget across possibly many
 * unrelated chunks.
 *
 * @param[in] rudp The rudp instance to use
 * @param[out] message The rudp message that will be filled in during the receive
 * @param[in] recv_type The RudpPacketType the caller wants to receive
 * @param[in] min_data_size The minimum acceptable data size for a matching message
 * @param[out] out_err Set to the underlying error code when the outcome is RUDP_RECV_ERROR
 * @return The classification outcome; see RudpRecvOutcome
 */
static RudpRecvOutcome rudp_recv_and_classify(RudpInstance *rudp, RudpMessage *message, RudpPacketType recv_type, size_t min_data_size, ChiakiErrorCode *out_err)
{
    ChiakiErrorCode err = chiaki_rudp_select_recv(rudp, 1500, message);
    if(err == CHIAKI_ERR_TIMEOUT)
        return RUDP_RECV_TIMEOUT;
    if(err != CHIAKI_ERR_SUCCESS)
    {
        *out_err = err;
        return RUDP_RECV_ERROR;
    }
    rudp_log_received_chunk(rudp, message);

    bool found = true;
    while(true)
    {
        switch(recv_type)
        {
            case INIT_RESPONSE:
                if(message->subtype != 0xD0)
                {
                    if(assign_submessage_to_message(message))
                        continue;
                    if(rudp_subtype_is_benign_control(message->subtype))
                        CHIAKI_LOGV(rudp->log, "Rudp send_recv: draining benign %s chunk (subtype 0x%02x) while waiting for INIT RESPONSE",
                            rudp_benign_subtype_name(message->subtype), message->subtype);
                    else
                    {
                        CHIAKI_LOGE(rudp->log, "Expected INIT RESPONSE with subtype 0xD0.\nReceived unexpected RUDP message ... draining");
                        chiaki_rudp_print_message(rudp, message);
                    }
                    chiaki_rudp_message_pointers_free(message);
                    found = false;
                    break;
                }
                break;
            case COOKIE_RESPONSE:
                if(message->subtype != 0xA0)
                {
                    if(assign_submessage_to_message(message))
                        continue;
                    if(rudp_subtype_is_benign_control(message->subtype))
                        CHIAKI_LOGV(rudp->log, "Rudp send_recv: draining benign %s chunk (subtype 0x%02x) while waiting for COOKIE RESPONSE",
                            rudp_benign_subtype_name(message->subtype), message->subtype);
                    else
                    {
                        CHIAKI_LOGE(rudp->log, "Expected COOKIE RESPONSE with subtype 0xA0.\nReceived unexpected RUDP message ... draining");
                        chiaki_rudp_print_message(rudp, message);
                    }
                    chiaki_rudp_message_pointers_free(message);
                    found = false;
                    break;
                }
                break;
            case CTRL_MESSAGE:
                if((message->subtype & 0x0F) != 0x2 && (message->subtype & 0x0F) != 0x6)
                {
                    if(assign_submessage_to_message(message))
                        continue;
                    if(rudp_subtype_is_benign_control(message->subtype))
                        CHIAKI_LOGV(rudp->log, "Rudp send_recv: draining benign %s chunk (subtype 0x%02x) while waiting for CTRL MESSAGE",
                            rudp_benign_subtype_name(message->subtype), message->subtype);
                    else
                    {
                        CHIAKI_LOGE(rudp->log, "Expected CTRL MESSAGE with subtype 0x2 or 0x36.\nReceived unexpected RUDP message ... draining");
                        chiaki_rudp_print_message(rudp, message);
                    }
                    chiaki_rudp_message_pointers_free(message);
                    found = false;
                    break;
                }
                break;
            case FINISH:
                if(message->subtype != 0xC0)
                {
                    if(assign_submessage_to_message(message))
                        continue;
                    if(rudp_subtype_is_benign_control(message->subtype))
                        CHIAKI_LOGV(rudp->log, "Rudp send_recv: draining benign %s chunk (subtype 0x%02x) while waiting for FINISH MESSAGE",
                            rudp_benign_subtype_name(message->subtype), message->subtype);
                    else
                    {
                        CHIAKI_LOGE(rudp->log, "Expected FINISH MESSAGE with subtype 0xC0 .\nReceived unexpected RUDP message ... draining");
                        chiaki_rudp_print_message(rudp, message);
                    }
                    chiaki_rudp_message_pointers_free(message);
                    found = false;
                    break;
                }
                break;
            default:
                CHIAKI_LOGE(rudp->log, "Selected RudpPacketType 0x%04x to receive that is not supported by rudp send receive.", recv_type);
                chiaki_rudp_message_pointers_free(message);
                *out_err = CHIAKI_ERR_INVALID_DATA;
                return RUDP_RECV_ERROR;
        }
        break;
    }
    if(!found)
        return RUDP_RECV_DRAIN;
    if(message->data_size < min_data_size)
    {
        chiaki_rudp_message_pointers_free(message);
        CHIAKI_LOGE(rudp->log, "Received message with too small of data size");
        return RUDP_RECV_TOO_SMALL;
    }
    return RUDP_RECV_MATCH;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_send_recv(RudpInstance *rudp, RudpMessage *message, uint8_t *buf, size_t buf_size, uint16_t remote_counter, RudpPacketType send_type, RudpPacketType recv_type, size_t min_data_size, size_t tries)
{
    bool success = false;
    bool have_retry_counter = false;
    uint16_t retry_local_counter = 0;
    // Total wall-clock budget (across all tries, all resends included) for draining benign peer control
    // traffic (Fix 1). Checked at the very top of the inner drain loop below, before each potentially
    // blocking receive -- so once it's exhausted, no further select_recv() calls happen on this or any
    // later try, keeping the real worst case close to `tries * RUDP_EXPECT_TIMEOUT_MS` (checking the
    // deadline only *after* a receive would let every remaining try still burn its own full timeout on
    // top of an already-exhausted drain budget, roughly doubling the worst case).
    uint64_t drain_deadline_ms = chiaki_time_now_monotonic_ms() + (uint64_t)tries * RUDP_EXPECT_TIMEOUT_MS;

    for(size_t i = 0; i < tries; i++)
    {
        // Fix 2: on the first attempt, mint a new local counter and remember it. On every retry of the
        // *same logical message*, resend with that identical counter instead of minting a new one --
        // otherwise the peer's selective-ack window can never catch up to a constantly moving target.
        switch(send_type)
        {
            case INIT_REQUEST:
                if(!have_retry_counter)
                {
                    retry_local_counter = get_then_increase_counter(rudp);
                    have_retry_counter = true;
                }
                rudp_send_init_message_with_counter(rudp, retry_local_counter);
                break;
            case COOKIE_REQUEST:
                if(!have_retry_counter)
                {
                    retry_local_counter = get_then_increase_counter(rudp);
                    have_retry_counter = true;
                }
                rudp_send_cookie_message_with_counter(rudp, retry_local_counter, buf, buf_size);
                break;
            case ACK:
                // chiaki_rudp_send_ack_message() already reuses rudp->counter as-is (no increment), so
                // no counter-reuse bookkeeping is needed here.
                chiaki_rudp_send_ack_message(rudp, remote_counter);
                break;
            case SESSION_MESSAGE:
                if(!have_retry_counter)
                {
                    retry_local_counter = get_then_increase_counter(rudp);
                    have_retry_counter = true;
                }
                rudp_send_session_message_with_counter(rudp, retry_local_counter, remote_counter, buf, buf_size);
                break;
            default:
                CHIAKI_LOGE(rudp->log, "Selected RudpPacketType 0x%04x to send that is not supported by rudp send receive.", send_type);
                return CHIAKI_ERR_INVALID_DATA;
        }

        // Fix 1: drain unrelated/benign chunks (selective-acks, gap-reports, stale FINISH, duplicate
        // responses, ...) without resending and without consuming a try. Only a real timeout or a
        // matching-but-too-small message falls through to the outer loop's resend.
        size_t drained = 0;
        for(;;)
        {
            // Checked first, before the (potentially ~RUDP_EXPECT_TIMEOUT_MS-blocking) receive below, so
            // an already-exhausted budget short-circuits immediately instead of burning another full
            // select_recv() wait -- see the comment on drain_deadline_ms above.
            if(chiaki_time_now_monotonic_ms() >= drain_deadline_ms)
                break; // wall-clock drain budget exhausted -> treat like a timeout, resend
            ChiakiErrorCode classify_err = CHIAKI_ERR_SUCCESS;
            RudpRecvOutcome outcome = rudp_recv_and_classify(rudp, message, recv_type, min_data_size, &classify_err);
            if(outcome == RUDP_RECV_ERROR)
                return classify_err;
            if(outcome == RUDP_RECV_MATCH)
            {
                success = true;
                break;
            }
            if(outcome == RUDP_RECV_TOO_SMALL || outcome == RUDP_RECV_TIMEOUT)
                break; // fall through to outer loop -> resend, consumes a try
            // RUDP_RECV_DRAIN: keep waiting for the real response, bounded by count (checked here) and
            // wall clock (checked at the top of this loop, above).
            drained++;
            if(drained >= RUDP_MAX_DRAIN_MESSAGES)
                break; // per-try drain-count budget exhausted -> treat like a timeout, resend
        }
        if(success)
            break;
    }
    if(success)
        return CHIAKI_ERR_SUCCESS;
    else
    {
        CHIAKI_LOGE(rudp->log, "Could not receive correct RUDP message after %lu tries", tries);
        print_rudp_message_type(rudp, recv_type);
        return CHIAKI_ERR_INVALID_RESPONSE;
    }
}

static bool assign_submessage_to_message(RudpMessage *message)
{
    if(message->subMessage)
    {
        if(message->data)
        {
            free(message->data);
            message->data = NULL;
        }
        RudpMessage *tmp = message->subMessage;
        memcpy(message, message->subMessage, sizeof(RudpMessage));
        free(tmp);
        return true;
    }
    return false;
}
CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_ack_packet(RudpInstance *rudp, uint16_t counter_to_ack)
{
	ChiakiSeqNum16 acked_seq_nums[RUDP_SEND_BUFFER_SIZE];
	size_t acked_seq_nums_count = 0;
	ChiakiErrorCode err = chiaki_rudp_send_buffer_ack(&rudp->send_buffer, counter_to_ack, acked_seq_nums, &acked_seq_nums_count);
    return err;
}

CHIAKI_EXPORT void chiaki_rudp_print_message(RudpInstance *rudp, RudpMessage *message)
{
    CHIAKI_LOGI(rudp->log, "-------------RUDP MESSAGE------------");
    print_rudp_message_type(rudp, message->type);
    CHIAKI_LOGI(rudp->log, "Rudp Message Subtype: 0x%02x", message->subtype);
    CHIAKI_LOGI(rudp->log, "Rudp Message Size: %02x", message->size);
    CHIAKI_LOGI(rudp->log, "Rudp Message Data Size: %lu", message->data_size);
    CHIAKI_LOGI(rudp->log, "-----Rudp Message Data ---");
    if(message->data)
        chiaki_log_hexdump(rudp->log, CHIAKI_LOG_INFO, message->data, message->data_size);
    CHIAKI_LOGI(rudp->log, "Rudp Message Remote Counter: %lu", message->remote_counter);
    if(message->subMessage)
        chiaki_rudp_print_message(rudp, message->subMessage);
}

CHIAKI_EXPORT void chiaki_rudp_message_pointers_free(RudpMessage *message)
{
    if(message->data)
    {
        free(message->data);
        message->data = NULL;
    }
    if(message->subMessage)
    {
        chiaki_rudp_message_pointers_free(message->subMessage);
        free(message->subMessage);
        message->subMessage = NULL;
    }
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_rudp_fini(RudpInstance *rudp)
{
    chiaki_rudp_send_buffer_fini(&rudp->send_buffer);
    CHIAKI_SOCKET_CLOSE(rudp->sock);
    rudp->sock = CHIAKI_INVALID_SOCKET;
    ChiakiErrorCode err = chiaki_mutex_fini(&rudp->counter_mutex);
    chiaki_stop_pipe_fini(&rudp->stop_pipe);
    if(rudp)
        free(rudp);
    return err;
}

/**
 * Prints a given rudp message type
 *
 * @param[in] rudp The rudp instance to use
 * @return type The type of packet to print
 *
*/
static void print_rudp_message_type(RudpInstance *rudp, RudpPacketType type)
{
    switch(type)
    {
        case INIT_REQUEST:
            CHIAKI_LOGI(rudp->log, "Message Type: Init Request");
            break;
        case INIT_RESPONSE:
            CHIAKI_LOGI(rudp->log, "Message Type: Init Response");
            break;
        case COOKIE_REQUEST:
            CHIAKI_LOGI(rudp->log, "Message Type: Cookie Request");
            break;
        case COOKIE_RESPONSE:
            CHIAKI_LOGI(rudp->log, "Message Type: Cookie Response");
            break;
        case SESSION_MESSAGE:
            CHIAKI_LOGI(rudp->log, "Message Type: Session Message");
            break;
        case STREAM_CONNECTION_SWITCH_ACK:
            CHIAKI_LOGI(rudp->log, "Message Type: Takion Switch Ack");
            break;
        case ACK:
            CHIAKI_LOGI(rudp->log, "Message Type: Ack");
            break;
        case CTRL_MESSAGE:
            CHIAKI_LOGI(rudp->log, "Message Type: Ctrl Message");
            break;
        case UNKNOWN:
            CHIAKI_LOGI(rudp->log, "Message Type: Unknown");
            break;
        case FINISH:
            CHIAKI_LOGI(rudp->log, "Message Type: Finish");
            break;
        default:
            // Subtype (high byte) 0x30 with a varying low byte (observed 0x302F/0x302E/0x302D, ...) is
            // a session-class selective-ack/gap-report chunk, not an unrecognized message -- see the
            // RudpPacketType enum comment in rudp.h.
            if(((type >> 8) & 0xFF) == 0x30)
                CHIAKI_LOGI(rudp->log, "Message Type: Session Gap-Report / Selective-Ack (0x%04x)", type);
            else
                CHIAKI_LOGI(rudp->log, "Unknown Message Type: %04x", type);
            break;
    }
}

