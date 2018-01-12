// Inspired from the DPDK null driver

#include <device_stub.h>

#include <stdbool.h>

#include <klee/klee.h>

#include <containers/str-descr.h>

#include <rte_byteorder.h>
#include <rte_dev.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>


static const int stub_devices_count = 4; // more devices = lots more paths because of possible failures
static const char* stub_driver_name = "stub";
static const char* stub_device_name_format = "stub_%d";
static struct ether_addr stub_addr = { .addr_bytes = {0} };
static struct rte_eth_link stub_link = {
	.link_speed = ETH_SPEED_NUM_10G,
	.link_duplex = ETH_LINK_FULL_DUPLEX,
	.link_status = ETH_LINK_DOWN,
	.link_autoneg = ETH_LINK_SPEED_AUTONEG
};

struct str_field_descr mbuf_descrs[] = {
  //Do not forget about "buf_addr" -- it is a pointer that is why it is not listed here.
  {offsetof(struct rte_mbuf, buf_physaddr), sizeof(uint64_t), "buf_physaddr"},
  {offsetof(struct rte_mbuf, buf_len), sizeof(uint16_t), "buf_len"},
  {offsetof(struct rte_mbuf, data_off), sizeof(uint16_t), "data_off"},
  {offsetof(struct rte_mbuf, refcnt), sizeof(uint16_t), "refcnt"},
  {offsetof(struct rte_mbuf, nb_segs), sizeof(uint8_t), "nb_segs"},
  {offsetof(struct rte_mbuf, port), sizeof(uint8_t), "port"},
  {offsetof(struct rte_mbuf, ol_flags), sizeof(uint64_t), "ol_flags"},
  {offsetof(struct rte_mbuf, packet_type), sizeof(uint32_t), "packet_type"},
  {offsetof(struct rte_mbuf, pkt_len), sizeof(uint32_t), "pkt_len"},
  {offsetof(struct rte_mbuf, data_len), sizeof(uint16_t), "data_len"},
  {offsetof(struct rte_mbuf, vlan_tci), sizeof(uint16_t), "vlan_tci"},
  {offsetof(struct rte_mbuf, hash), sizeof(uint32_t), "hash"},
  {offsetof(struct rte_mbuf, seqn), sizeof(uint32_t), "seqn"},
  {offsetof(struct rte_mbuf, vlan_tci_outer), sizeof(uint16_t), "vlan_tci_outer"},
  {offsetof(struct rte_mbuf, udata64), sizeof(uint64_t), "udata64"},
  /*{offsetof(struct rte_mbuf, pool), sizeof(void*), "pool"}, TODO no real reason this is commented out? */
  /*{offsetof(struct rte_mbuf, next), sizeof(struct rte_mbuf*), "next"}, - we ban access to it, so let's not trace it */
  {offsetof(struct rte_mbuf, tx_offload), sizeof(uint64_t), "tx_offload"},
  {offsetof(struct rte_mbuf, priv_size), sizeof(uint16_t), "priv_size"},
  {offsetof(struct rte_mbuf, timesync), sizeof(uint16_t), "timesync"},
};
struct nested_field_descr stub_mbuf_content_nested[] = {
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, ether_type), sizeof(uint16_t), "ether_type"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, d_addr), sizeof(struct ether_addr), "d_addr"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, s_addr), sizeof(struct ether_addr), "s_addr"},
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, version_ihl), sizeof(uint8_t), "version_ihl"},
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, type_of_service), sizeof(uint8_t), "type_of_service"},
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, total_length), sizeof(uint16_t), "total_length"},
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, packet_id), sizeof(uint16_t), "packet_id"},
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, fragment_offset), sizeof(uint16_t), "fragment_offset"},
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, time_to_live), sizeof(uint8_t), "time_to_live"},
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, next_proto_id), sizeof(uint8_t), "next_proto_id"},
  /*
    skip the checksum, as it is very hard to verify symbolically.
    {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, hdr_checksum),
    sizeof(uint16_t), "hdr_checksum"},
  */
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, src_addr), sizeof(uint32_t), "src_addr"},
  {offsetof(struct stub_mbuf_content, ipv4), offsetof(struct ipv4_hdr, dst_addr), sizeof(uint32_t), "dst_addr"},

  {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, src_port), sizeof(uint16_t), "src_port"},
  {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, dst_port), sizeof(uint16_t), "dst_port"},
  {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, sent_seq), sizeof(uint32_t), "sent_seq"},
  {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, recv_ack), sizeof(uint32_t), "recv_ack"},
  {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, data_off), sizeof(uint8_t), "data_off"},
  {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, tcp_flags), sizeof(uint8_t), "tcp_flags"},
  {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, rx_win), sizeof(uint16_t), "rx_win"},
  /*
    skip the checksum, as it is very hard to verify symbolically.
    {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, cksum),
    sizeof(uint16_t), "cksum"},
  */
  {offsetof(struct stub_mbuf_content, tcp), offsetof(struct tcp_hdr, tcp_urp), sizeof(uint16_t), "tcp_urp"},
};
struct nested_nested_field_descr stub_mbuf_content_n2[] = {
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, d_addr), 0, sizeof(uint8_t), "a"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, d_addr), 1, sizeof(uint8_t), "b"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, d_addr), 2, sizeof(uint8_t), "c"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, d_addr), 3, sizeof(uint8_t), "d"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, d_addr), 4, sizeof(uint8_t), "e"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, d_addr), 5, sizeof(uint8_t), "f"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, s_addr), 0, sizeof(uint8_t), "a"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, s_addr), 1, sizeof(uint8_t), "b"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, s_addr), 2, sizeof(uint8_t), "c"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, s_addr), 3, sizeof(uint8_t), "d"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, s_addr), 4, sizeof(uint8_t), "e"},
  {offsetof(struct stub_mbuf_content, ether), offsetof(struct ether_hdr, s_addr), 5, sizeof(uint8_t), "f"},
};
#define KLEE_TRACE_MBUF(m_ptr, dir)                                                                                                    \
  klee_trace_param_ptr_directed(m_ptr, sizeof(*m_ptr), #m_ptr, dir);                                                                   \
  klee_trace_param_ptr_field_directed(m_ptr, offsetof(struct rte_mbuf, buf_addr), sizeof(struct stub_mbuf_content*), "buf_addr", dir); \
  for (int i = 0; i < sizeof(mbuf_descrs)/sizeof(mbuf_descrs[0]); ++i) {                                                               \
    klee_trace_param_ptr_field_directed(m_ptr,                                                                                         \
                                        mbuf_descrs[i].offset,                                                                         \
                                        mbuf_descrs[i].width,                                                                          \
                                        mbuf_descrs[i].name,                                                                           \
                                        dir);                                                                                          \
  }
#define KLEE_TRACE_MBUF_EPTR(m_ptr, pname, dir)                                                                               \
  klee_trace_extra_ptr(m_ptr, sizeof(*m_ptr), pname, "", dir);                                                                \
  klee_trace_extra_ptr_field(m_ptr, offsetof(struct rte_mbuf, buf_addr), sizeof(struct stub_mbuf_content*), "buf_addr", dir); \
  for (int i = 0; i < sizeof(mbuf_descrs)/sizeof(mbuf_descrs[0]); ++i) {                                                      \
    klee_trace_extra_ptr_field(m_ptr,                                                                                         \
                               mbuf_descrs[i].offset,                                                                         \
                               mbuf_descrs[i].width,                                                                          \
                               mbuf_descrs[i].name,                                                                           \
                               dir);                                                                                          \
  }
#define KLEE_TRACE_MBUF_CONTENT(u_ptr, dir)                                                                             \
  klee_trace_extra_ptr(u_ptr, sizeof(struct stub_mbuf_content), "user_buf_addr", "", dir);                              \
  klee_trace_extra_ptr_field(u_ptr, offsetof(struct stub_mbuf_content, ether), sizeof(struct ether_hdr), "ether", dir); \
  klee_trace_extra_ptr_field(u_ptr, offsetof(struct stub_mbuf_content, ipv4), sizeof(struct ipv4_hdr), "ipv4", dir);    \
  klee_trace_extra_ptr_field(u_ptr, offsetof(struct stub_mbuf_content, tcp), sizeof(struct tcp_hdr), "tcp", dir);       \
  for (int i = 0; i < sizeof(stub_mbuf_content_nested)/sizeof(stub_mbuf_content_nested[0]); ++i) {                      \
    klee_trace_extra_ptr_nested_field(u_ptr,                                                                            \
                                      stub_mbuf_content_nested[i].base_offset,                                          \
                                      stub_mbuf_content_nested[i].offset,                                               \
                                      stub_mbuf_content_nested[i].width,                                                \
                                      stub_mbuf_content_nested[i].name,                                                 \
                                      dir);                                                                             \
  }                                                                                                                     \
  for (int i = 0; i < sizeof(stub_mbuf_content_n2)/sizeof(stub_mbuf_content_n2[0]); ++i) {                              \
    klee_trace_extra_ptr_nested_nested_field                                                                            \
      (u_ptr,                                                                                                           \
       stub_mbuf_content_n2[i].base_base_offset,                                                                        \
       stub_mbuf_content_n2[i].base_offset,                                                                             \
       stub_mbuf_content_n2[i].offset,                                                                                  \
       stub_mbuf_content_n2[i].width,                                                                                   \
       stub_mbuf_content_n2[i].name,                                                                                    \
       dir);                                                                                                            \
  }


// "Real" free function, not traced; stub tx and free both call it
void
real_free(struct rte_mbuf* mbuf) {
	// Undo our pseudo-chain trickery (see stub_rx)
	klee_allow_access(mbuf->next, mbuf->pool->elt_size);
	free(mbuf->next);
	mbuf->next = NULL;

	klee_alias_function("rte_pktmbuf_free", "rte_pktmbuf_free");
	rte_pktmbuf_free(mbuf);
	klee_alias_function("rte_pktmbuf_free", "stub_free");
}

void
stub_free(struct rte_mbuf* mbuf) {
	klee_trace_ret();
	KLEE_TRACE_MBUF(mbuf, TD_IN);
	KLEE_TRACE_MBUF_CONTENT(mbuf->buf_addr, TD_IN);

	real_free(mbuf);
}

static uint16_t
stub_rx(void* q, struct rte_mbuf** bufs, uint16_t nb_bufs)
{
	klee_assert(q != NULL);
	klee_assert(nb_bufs == 1); // :(

	struct stub_queue* stub_q = q;

	uint16_t priv_size = rte_pktmbuf_priv_size(stub_q->mb_pool);
	uint16_t mbuf_size = sizeof(struct rte_mbuf) + priv_size;
	uint16_t buf_len = rte_pktmbuf_data_room_size(stub_q->mb_pool);

	int received = klee_range(0, nb_bufs + 1 /* end is exclusive */, "received");
	int i; // Concrete return value, validator doesn't like return symbols
	for (i = 0; i < received; i++) {
		bufs[i] = rte_mbuf_raw_alloc(stub_q->mb_pool);
		if (!bufs[i]) {
			break;
		}

		struct rte_mbuf* buf_symbol = (struct rte_mbuf*) malloc(stub_q->mb_pool->elt_size);
		if (buf_symbol == NULL) {
			rte_pktmbuf_free(bufs[i]);
			break;
		}

		// Make the packet symbolic
		klee_make_symbolic(buf_symbol, stub_q->mb_pool->elt_size, "buf_value");
		memcpy(bufs[i], buf_symbol, stub_q->mb_pool->elt_size);
		free(buf_symbol);

		// Explicitly make the content symbolic - validator depends on an user_buf symbol for the proof
		struct stub_mbuf_content* buf_content_symbol = (struct stub_mbuf_content*) malloc(sizeof(struct stub_mbuf_content));
		if (buf_content_symbol == NULL) {
			rte_pktmbuf_free(bufs[i]);
			break;
		}
		klee_make_symbolic(buf_content_symbol, sizeof(struct stub_mbuf_content), "user_buf");
		memcpy((char*) bufs[i] + mbuf_size, buf_content_symbol, sizeof(struct stub_mbuf_content));
		free(buf_content_symbol);

		// We do not support chained mbufs for now, make sure the NF doesn't touch them
		struct rte_mbuf* buf_next = (struct rte_mbuf*) malloc(stub_q->mb_pool->elt_size);
                if (buf_next == NULL) {
                        rte_pktmbuf_free(bufs[i]);
                        break;
                }
		bufs[i]->next = buf_next;
		klee_forbid_access(bufs[i]->next, stub_q->mb_pool->elt_size, "buf_next");

		// Keep contrete values for what a driver guarantees
		// (assignments are in the same order as the rte_mbuf declaration)
		bufs[i]->buf_addr = (char*) bufs[i] + mbuf_size;
		bufs[i]->buf_physaddr = rte_mempool_virt2phy(stub_q->mb_pool, bufs[i]) + mbuf_size;
		bufs[i]->buf_len = (uint16_t) buf_len;
		// TODO: make data_off symbolic (but then we get symbolic pointer addition...)
		// Alternative: Somehow prove that the code never touches anything outside of the [data_off, data_off+data_len] range...
		bufs[i]->data_off = 0; // klee_range(0, stub_q->mb_pool->elt_size - sizeof(struct stub_mbuf_content), "data_off");
		bufs[i]->refcnt = 1;
		bufs[i]->nb_segs = 1; // TODO do we want to make a possibility of multiple packets? Or we could just prove the NF never touches this...
		bufs[i]->port = stub_q->port_id;
		bufs[i]->ol_flags = 0;
		// packet_type is symbolic
		bufs[i]->pkt_len = sizeof(struct stub_mbuf_content);
		bufs[i]->data_len = sizeof(struct stub_mbuf_content); // TODO ideally those should be symbolic...
		// vlan_tci is symbolic
		// hash is symbolic
		// seqn is symbolic
		// vlan_tci_outer is symbolic
		bufs[i]->userdata = NULL;
		bufs[i]->pool = stub_q->mb_pool;
		// tx_offload is symbolic
		bufs[i]->priv_size = priv_size;
		// timesync is symbolic

		rte_mbuf_sanity_check(bufs[i], 1 /* is head mbuf */);

		struct stub_mbuf_content* buf_content = rte_pktmbuf_mtod(bufs[i], struct stub_mbuf_content*);
		// TODO this matches nf_util, fix at the same time
		if(RTE_ETH_IS_IPV4_HDR(bufs[i]->packet_type) ||
			(bufs[i]->packet_type == 0 && buf_content->ether.ether_type == rte_cpu_to_be_16(ETHER_TYPE_IPv4))) {
			// TODO can we make version_ihl symbolic?
			buf_content->ipv4.version_ihl = (4 << 4) | 5; // IPv4, 5x4 bytes - concrete to avoid symbolic indexing
			buf_content->ipv4.total_length = rte_cpu_to_be_16(sizeof(struct ipv4_hdr) + sizeof(struct tcp_hdr));
		}
	}

	// Zero out the space we haven't used
	for (int j = i; j < nb_bufs; j++) {
		bufs[j] = NULL;
	}

	klee_trace_ret();
	klee_trace_param_ptr_directed(q, sizeof(struct stub_queue), "q", TD_IN);
	klee_trace_param_ptr_field_directed(q, offsetof(struct stub_queue, port_id), sizeof(uint16_t), "port_id", TD_IN);
	klee_trace_param_ptr_directed(bufs, sizeof(struct rte_mbuf*), "mbuf", TD_BOTH);
	klee_trace_param_u16(nb_bufs, "nb_bufs");

	if (i > 0) {
		// TODO should trace every packet... but for now there's only 1 anyway
		// Packet is actually received, trace it now
		KLEE_TRACE_MBUF_EPTR(bufs[0], "incoming_package", TD_OUT);
		KLEE_TRACE_MBUF_CONTENT(bufs[0]->buf_addr, TD_OUT);
	}

	return i;
}

static uint16_t
stub_tx(void* q, struct rte_mbuf** bufs, uint16_t nb_bufs)
{
	klee_assert(q != NULL);
	klee_assert(nb_bufs == 1); // :(

	klee_trace_ret();
	klee_trace_param_ptr_directed(q, sizeof(struct stub_queue), "q", TD_IN);
	klee_trace_param_ptr_field_directed(q, offsetof(struct stub_queue, port_id), sizeof(uint16_t), "port_id", TD_IN);
	struct rte_mbuf* mbuf = bufs[0]; KLEE_TRACE_MBUF(mbuf, TD_IN); // TODO fix this silly macro to take a name, and document the horrible single-pointer hack or fix the damn thing
	klee_trace_param_u16(nb_bufs, "nb_bufs");
	KLEE_TRACE_MBUF_CONTENT(bufs[0]->buf_addr, TD_IN);

	int packets_sent = klee_range(0, nb_bufs + 1 /* end is exclusive */, "packets_sent");
	int i; // Concrete return value, validator doesn't like symbols
	for (i = 0; i < packets_sent; i++) {
		real_free(bufs[i]);
	}

	return i;
}

static int
stub_dev_configure(struct rte_eth_dev *dev)
{
	return klee_int("dev_configure_return");
}

static int
stub_dev_start(struct rte_eth_dev *dev)
{
	int ret = klee_int("dev_start_return");
	if (ret == 0) {
		dev->data->dev_link.link_status = ETH_LINK_UP;
	}
	return ret;
}

static void
stub_dev_stop(struct rte_eth_dev *dev)
{
	dev->data->dev_link.link_status = ETH_LINK_DOWN;
}

static int
stub_rx_queue_setup(struct rte_eth_dev *dev, uint16_t rx_queue_id,
		uint16_t nb_rx_desc,
		unsigned int socket_id,
		const struct rte_eth_rxconf *rx_conf,
		struct rte_mempool *mb_pool)
{
	if (rx_queue_id >= dev->data->nb_rx_queues) {
		return -ENODEV;
	}

	int ret = klee_int("dev_rx_queue_setup_return");

	if (ret == 0) {
		struct stub_device* device = dev->data->dev_private;
		device->rx_queues[rx_queue_id].port_id = dev->data->port_id;
		device->rx_queues[rx_queue_id].mb_pool = mb_pool;

		dev->data->rx_queues[rx_queue_id] = &device->rx_queues[rx_queue_id];
	}

	return ret;
}

static int
stub_tx_queue_setup(struct rte_eth_dev *dev, uint16_t tx_queue_id,
		uint16_t nb_tx_desc,
		unsigned int socket_id,
		const struct rte_eth_txconf *tx_conf)
{
	if (tx_queue_id >= dev->data->nb_tx_queues) {
		return -ENODEV;
	}

	int ret = klee_int("dev_tx_queue_setup_return");

	if (ret == 0) {
	 	struct stub_device* device = dev->data->dev_private;
		device->tx_queues[tx_queue_id].port_id = dev->data->port_id;
		device->tx_queues[tx_queue_id].mb_pool = NULL;

		dev->data->tx_queues[tx_queue_id] = &device->tx_queues[tx_queue_id];
	}

	return ret;
}


static void
stub_queue_release(void *q)
{
	if (q != NULL) {
		klee_forbid_access(q, sizeof(struct stub_queue), "released_queue");
	}
}

static void
stub_dev_info(struct rte_eth_dev *dev,
		struct rte_eth_dev_info *dev_info)
{
	struct stub_device* device = dev->data->dev_private;

	dev_info->driver_name = stub_driver_name;
	dev_info->max_mac_addrs = 1;
	dev_info->max_rx_pktlen = (uint32_t) -1;
	dev_info->max_rx_queues = RTE_DIM(device->rx_queues);
	dev_info->max_tx_queues = RTE_DIM(device->tx_queues);
	dev_info->min_rx_bufsize = 0;
	dev_info->pci_dev = NULL;
	dev_info->reta_size = 0;
	dev_info->flow_type_rss_offloads = 0;
}

static void
stub_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *igb_stats)
{
	klee_abort();
}

static void
stub_stats_reset(struct rte_eth_dev *dev)
{
	klee_abort();
}

static int
stub_link_update(struct rte_eth_dev *dev, int wait_to_complete)
{
	return klee_int("dev_link_update_return");
}

static int
stub_rss_reta_update(struct rte_eth_dev *dev, struct rte_eth_rss_reta_entry64 *reta_conf, uint16_t reta_size)
{
	klee_abort();
}

static int
stub_rss_reta_query(struct rte_eth_dev *dev, struct rte_eth_rss_reta_entry64 *reta_conf, uint16_t reta_size)
{
	klee_abort();
}

static int
stub_rss_hash_update(struct rte_eth_dev *dev, struct rte_eth_rss_conf *rss_conf)
{
	klee_abort();
}

static int
stub_rss_hash_conf_get(struct rte_eth_dev *dev,
		struct rte_eth_rss_conf *rss_conf)
{
	klee_abort();
}

static const struct eth_dev_ops stub_ops = {
	.dev_start = stub_dev_start,
	.dev_stop = stub_dev_stop,
	.dev_configure = stub_dev_configure,
	.dev_infos_get = stub_dev_info,
	.rx_queue_setup = stub_rx_queue_setup,
	.tx_queue_setup = stub_tx_queue_setup,
	.rx_queue_release = stub_queue_release,
	.tx_queue_release = stub_queue_release,
	.link_update = stub_link_update,
	.stats_get = stub_stats_get,
	.stats_reset = stub_stats_reset,
	.reta_update = stub_rss_reta_update,
	.reta_query = stub_rss_reta_query,
	.rss_hash_update = stub_rss_hash_update,
	.rss_hash_conf_get = stub_rss_hash_conf_get
};

int
stub_dev_create(const char* name,
		const unsigned numa_node)
{
	int ret = klee_int("dev_create_return");
	if (ret != 0) {
		return ret;
	}

	struct stub_device* device = (struct stub_device*) malloc(sizeof(struct stub_device));
	if (device == NULL) {
		return -ENOMEM;
	}

	struct rte_eth_dev* eth_dev = rte_eth_dev_allocate(name, RTE_ETH_DEV_PCI);
	if (eth_dev == NULL) {
		free(device);
		return -ENOMEM;
	}

	memset(device, 0, sizeof(struct stub_device));

	eth_dev->data->dev_private = device;
	eth_dev->data->nb_rx_queues = 1;
	eth_dev->data->nb_tx_queues = 1;
	eth_dev->data->dev_started = 0;
	eth_dev->data->dev_link = stub_link;
	eth_dev->data->mac_addrs = &stub_addr;
	eth_dev->data->dev_flags = 0;
	eth_dev->data->kdrv = RTE_KDRV_NONE;
	eth_dev->data->drv_name = stub_driver_name;
	eth_dev->data->numa_node = numa_node;

	eth_dev->dev_ops = &stub_ops;
	TAILQ_INIT(&eth_dev->link_intr_cbs);
	eth_dev->driver = NULL;
	eth_dev->rx_pkt_burst = stub_rx;
	eth_dev->tx_pkt_burst = stub_tx;

	return 0;
}

int
stub_dev_delete(struct rte_eth_dev* eth_dev)
{
	free(eth_dev->data->dev_private);
	free(eth_dev->data);

	return rte_eth_dev_release_port(eth_dev);
}

static char*
stub_device_name(int n)
{
	int length = snprintf(NULL, 0, stub_device_name_format, n) + 1;

	char* buffer = (char*) malloc(length);
	if (buffer == NULL) {
		return NULL;
	}

	snprintf(buffer, length, stub_device_name_format, n);
	return buffer;
}

static int
stub_devinit(const char *name, const char *params)
{
	// name is always NULL

        int ret = klee_int("devinit_return");
        if (ret != 0) {
                return ret;
        }

	unsigned numa_node = rte_socket_id();

	for (int n = 0; n < stub_devices_count; n++) {
		char* name = stub_device_name(n);
		if (name == NULL) {
			return -ENOMEM;
		}

		int ret = stub_dev_create(name, numa_node);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int
stub_devuninit(const char *name)
{
	// name is always NULL

	int ret = klee_int("devuninit_return");
	if (ret != 0) {
		return ret;
	}

	bool end = false;
	for (int n = 0; n < stub_devices_count; n++) {
		char* name = stub_device_name(n);
		if (name == NULL) {
			return -ENOMEM;
		}

		struct rte_eth_dev* eth_dev = rte_eth_dev_allocated(name);
		if (eth_dev == NULL) {
			end = true;
			continue;
		}
		if (end) {
			// There was an unallocated device before
			// but the current one is allocated - nonsensical!
			klee_abort();
		}

		stub_dev_delete(eth_dev);
	}

	return ret;
}

struct rte_driver stub_driver = {
	.name = RTE_STR(stub_name),
	.type = PMD_PDEV,
	.init = stub_devinit,
	.uninit = stub_devuninit,
};

void
stub_panic(const char *funcname, const char *format, ...)
{
	klee_silent_exit(1);
}

void
stub_init(void)
{
	// If DPDK tries to commit suicide, translate that in KLEE terms...
	klee_alias_function("__rte_panic", "stub_panic");

	// HACK
	klee_alias_function("rte_pktmbuf_free", "stub_free");
	// rte_pktmbuf_free is 'static inline', so it gets duplicated...
	klee_alias_function("rte_pktmbuf_free930", "stub_free");
	klee_alias_function("rte_pktmbuf_free1108", "stub_free");
	klee_alias_function("rte_pktmbuf_free1144", "stub_free");
	klee_alias_function("rte_pktmbuf_free1173", "stub_free");
	klee_alias_function("rte_pktmbuf_free1211", "stub_free");
	klee_alias_function("rte_pktmbuf_free1307", "stub_free");
	klee_alias_function("rte_pktmbuf_free1651", "stub_free");
	klee_alias_function("rte_pktmbuf_free1731", "stub_free");
	klee_alias_function("rte_pktmbuf_free1766", "stub_free");
	klee_alias_function("rte_pktmbuf_free1806", "stub_free");
	klee_alias_function("rte_pktmbuf_free1841", "stub_free");
	klee_alias_function("rte_pktmbuf_free1882", "stub_free");
	klee_alias_function("rte_pktmbuf_free1916", "stub_free");
	klee_alias_function("rte_pktmbuf_free1959", "stub_free");
	klee_alias_function("rte_pktmbuf_free2012", "stub_free");
	klee_alias_function("rte_pktmbuf_free2093", "stub_free");
	klee_alias_function("rte_pktmbuf_free2293", "stub_free");

	rte_eal_driver_register(&stub_driver);
}
