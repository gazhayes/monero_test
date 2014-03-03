// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <boost/foreach.hpp>
#include "include_base_utils.h"
using namespace epee;

#include "core_rpc_server.h"
#include "common/command_line.h"
#include "cryptonote_core/cryptonote_format_utils.h"
#include "cryptonote_core/account.h"
#include "misc_language.h"
#include "crypto/hash.h"
#include "core_rpc_server_error_codes.h"

namespace cryptonote
{
  namespace
  {
    const command_line::arg_descriptor<std::string> arg_rpc_bind_ip   = {"rpc-bind-ip", "", "127.0.0.1"};
    const command_line::arg_descriptor<std::string> arg_rpc_bind_port = {"rpc-bind-port", "", std::to_string(RPC_DEFAULT_PORT)};
  }

  //-----------------------------------------------------------------------------------
  void core_rpc_server::init_options(boost::program_options::options_description& desc)
  {
    command_line::add_arg(desc, arg_rpc_bind_ip);
    command_line::add_arg(desc, arg_rpc_bind_port);
  }
  //------------------------------------------------------------------------------------------------------------------------------
  core_rpc_server::core_rpc_server(core& cr, nodetool::node_server<cryptonote::t_cryptonote_protocol_handler<cryptonote::core> >& p2p):m_core(cr), m_p2p(p2p)
  {}
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::handle_command_line(const boost::program_options::variables_map& vm)
  {
    m_bind_ip = command_line::get_arg(vm, arg_rpc_bind_ip);
    m_port = command_line::get_arg(vm, arg_rpc_bind_port);
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::init(const boost::program_options::variables_map& vm)
  {
    m_net_server.set_threads_prefix("RPC");
    bool r = handle_command_line(vm);
    CHECK_AND_ASSERT_MES(r, false, "Failed to process command line in core_rpc_server");
    return epee::http_server_impl_base<core_rpc_server>::init(m_port, m_bind_ip);
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_get_height(const COMMAND_RPC_GET_HEIGHT::request& req, COMMAND_RPC_GET_HEIGHT::response& res)
  {
    res.height = m_core.get_current_blockchain_height();
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_get_info(const COMMAND_RPC_GET_INFO::request& req, COMMAND_RPC_GET_INFO::response& res)
  {
    res.height = m_core.get_current_blockchain_height();
    res.difficulty = m_core.get_blockchain_storage().get_difficulty_for_next_block();
    res.tx_count = m_core.get_blockchain_storage().get_total_transactions() - res.height; //without coinbase
    res.tx_pool_size = m_core.get_pool_transactions_count();
    res.alt_blocks_count = m_core.get_blockchain_storage().get_alternative_blocks_count();
    uint64_t total_conn = m_p2p.get_connections_count();
    res.outgoing_connections_count = m_p2p.get_outgoing_connections_count();
    res.incoming_connections_count = total_conn - res.outgoing_connections_count;
    res.white_peerlist_size = m_p2p.get_peerlist_manager().get_white_peers_count();
    res.grey_peerlist_size = m_p2p.get_peerlist_manager().get_gray_peers_count();
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_get_known_block_ids(const COMMAND_RPC_GET_KNOWN_BLOCK_IDS::request& req, COMMAND_RPC_GET_KNOWN_BLOCK_IDS::response& res)
  {
      std::list<crypto::hash> main, alt, invalid;
      m_core.get_all_known_block_ids(main, alt, invalid);
      BOOST_FOREACH(crypto::hash &h, main)
        res.main.push_back(string_tools::pod_to_hex(h));

      return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_get_blocks(const COMMAND_RPC_GET_BLOCKS_FAST::request& req, COMMAND_RPC_GET_BLOCKS_FAST::response& res)
  {
    std::list<std::pair<block, std::list<transaction> > > bs;
    if(!m_core.find_blockchain_supplement(req.block_ids, bs, res.current_height, res.start_height, COMMAND_RPC_GET_BLOCKS_FAST_MAX_COUNT))
    {
      res.status = "Failed";
      return false;
    }

    BOOST_FOREACH(auto& b, bs)
    {
      res.blocks.resize(res.blocks.size()+1);
      res.blocks.back().block = block_to_blob(b.first);
      BOOST_FOREACH(auto& t, b.second)
      {
        res.blocks.back().txs.push_back(tx_to_blob(t));
      }
    }

    res.status = CORE_RPC_STATUS_OK;
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_get_random_outs(const COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::request& req, COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::response& res)
  {
    res.status = "Failed";
    if(!m_core.get_random_outs_for_amounts(req, res))
    {
      return true;
    }

    res.status = CORE_RPC_STATUS_OK;
    std::stringstream ss;
    typedef COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::outs_for_amount outs_for_amount;
    typedef COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS::out_entry out_entry;
    std::for_each(res.outs.begin(), res.outs.end(), [&](outs_for_amount& ofa)
            {
              ss << "[" << ofa.amount << "]:";
              CHECK_AND_ASSERT_MES(ofa.outs.size(), ;, "internal error: ofa.outs.size() is empty for amount " << ofa.amount);
              std::for_each(ofa.outs.begin(), ofa.outs.end(), [&](out_entry& oe)
                  {
                    ss << oe.global_amount_index << " ";
                  });
              ss << ENDL;
            });
    std::string s = ss.str();
    LOG_PRINT_L2("COMMAND_RPC_GET_RANDOM_OUTPUTS_FOR_AMOUNTS: " << ENDL << s);
    res.status = CORE_RPC_STATUS_OK;
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_get_indexes(const COMMAND_RPC_GET_TX_GLOBAL_OUTPUTS_INDEXES::request& req, COMMAND_RPC_GET_TX_GLOBAL_OUTPUTS_INDEXES::response& res)
  {
    bool r = m_core.get_tx_outputs_gindexs(req.txid, res.o_indexes);
    if(!r)
    {
      res.status = "Failed";
      return true;
    }
    res.status = CORE_RPC_STATUS_OK;
    LOG_PRINT_L2("COMMAND_RPC_GET_TX_GLOBAL_OUTPUTS_INDEXES: [" << res.o_indexes.size() << "]");
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_get_transactions(const COMMAND_RPC_GET_TRANSACTIONS::request& req, COMMAND_RPC_GET_TRANSACTIONS::response& res)
  {
    std::vector<crypto::hash> vh;
    BOOST_FOREACH(const auto& tx_hex_str, req.txs_hashes)
    {
      blobdata b;
      if(!string_tools::parse_hexstr_to_binbuff(tx_hex_str, b))
      {
        res.status = "Failed to parse hex representation of transaction hash";
        return true;
      }
      if(b.size() != sizeof(crypto::hash))
      {
        res.status = "Failed, size of data mismatch";
      }
      vh.push_back(*reinterpret_cast<const crypto::hash*>(b.data()));
    }
    std::list<crypto::hash> missed_txs;
    std::list<transaction> txs;
    bool r = m_core.get_transactions(vh, txs, missed_txs);
    if(!r)
    {
      res.status = "Failed";
      return true;
    }

    BOOST_FOREACH(auto& tx, txs)
    {
      blobdata blob = t_serializable_object_to_blob(tx);
      res.txs_as_hex.push_back(string_tools::buff_to_hex_nodelimer(blob));
    }

    BOOST_FOREACH(const auto& miss_tx, missed_txs)
    {
      res.missed_tx.push_back(string_tools::pod_to_hex(miss_tx));
    }

    res.status = CORE_RPC_STATUS_OK;
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  /*bool core_rpc_server::on_get_outputs(const COMMAND_RPC_GET_OUTPUTS::request& req, COMMAND_RPC_GET_OUTPUTS::response& res)
  {
    return true;
  }*/
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_send_raw_tx(const COMMAND_RPC_SEND_RAW_TX::request& req, COMMAND_RPC_SEND_RAW_TX::response& res)
  {
    std::string tx_blob;
    if(!string_tools::parse_hexstr_to_binbuff(req.tx_as_hex, tx_blob))
    {
      LOG_PRINT_L0("[on_send_raw_tx]: Failed to parse tx from hexbuff: " << req.tx_as_hex);
      res.status = "Failed";
      return true;
    }

    cryptonote_connection_context fake_context = AUTO_VAL_INIT(fake_context);
    tx_verification_context tvc = AUTO_VAL_INIT(tvc);
    if(!m_core.handle_incoming_tx(tx_blob, tvc, false))
    {
      LOG_PRINT_L0("[on_send_raw_tx]: Failed to process tx");
      res.status = "Failed";
      return true;
    }

    if(tvc.m_verifivation_failed)
    {
      LOG_PRINT_L0("[on_send_raw_tx]: tx verification failed");
      res.status = "Failed";
      return true;
    }

    if(!tvc.m_should_be_relayed)
    {
      LOG_PRINT_L0("[on_send_raw_tx]: tx accepted, but not relayed");
      res.status = "Not relayed";
      return true;
    }


    NOTIFY_NEW_TRANSACTIONS::request r;
    r.txs.push_back(tx_blob);
    m_core.get_protocol()->relay_transactions(r, fake_context);
    //TODO: make sure that tx has reached other nodes here, probably wait to receive reflections from other nodes
    res.status = CORE_RPC_STATUS_OK;
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_start_mining(const COMMAND_RPC_START_MINING::request& req, COMMAND_RPC_START_MINING::response& res)
  {
    account_public_address adr;
    if(!get_account_address_from_str(adr, req.miner_address))
    {
      res.status = "Failed, wrong address";
      return true;
    }

    if(!m_core.get_miner().start(adr, static_cast<size_t>(req.threads_count)))
    {
      res.status = "Failed, mining not started";
      return true;
    }
    res.status = CORE_RPC_STATUS_OK;
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_stop_mining(const COMMAND_RPC_STOP_MINING::request& req, COMMAND_RPC_STOP_MINING::response& res)
  {

    if(!m_core.get_miner().stop())
    {
      res.status = "Failed, mining not stopped";
      return true;
    }
    res.status = CORE_RPC_STATUS_OK;
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_getblockcount(const COMMAND_RPC_GETBLOCKCOUNT::request& req, COMMAND_RPC_GETBLOCKCOUNT::response& res)
  {
    res = m_core.get_current_blockchain_height();
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_getblockhash(const COMMAND_RPC_GETBLOCKHASH::request& req, COMMAND_RPC_GETBLOCKHASH::response& res, epee::json_rpc::error& error_resp)
  {
    if(req.size() != 1)
    {
      error_resp.code = CORE_RPC_ERROR_CODE_WRONG_PARAM;
      error_resp.message = "Wrong parameters, expected height";
      return false;
    }
    uint64_t h = req[0];
    if(m_core.get_current_blockchain_height() <= h)
    {
      error_resp.code = CORE_RPC_ERROR_CODE_TOO_BIG_HEIGHT;
      error_resp.message = std::string("To big height: ") + std::to_string(h) + ", current blockchain height = " +  std::to_string(m_core.get_current_blockchain_height());
    }
    res = string_tools::pod_to_hex(m_core.get_block_id_by_height(h));
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  uint64_t slow_memmem(void* start_buff, size_t buflen,void* pat,size_t patlen)
  {
    void* buf = start_buff;
    void* end=(char*)buf+buflen-patlen;
    while((buf=memchr(buf,((char*)pat)[0],buflen)))
    {
      if(buf>end)
        return 0;
      if(memcmp(buf,pat,patlen)==0)
        return (char*)buf - (char*)start_buff;
      buf=(char*)buf+1;
    }
    return 0;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_getblocktemplate(const COMMAND_RPC_GETBLOCKTEMPLATE::request& req, COMMAND_RPC_GETBLOCKTEMPLATE::response& res, epee::json_rpc::error& error_resp)
  {
    if(req.reserve_size > 255)
    {
      error_resp.code = CORE_RPC_ERROR_CODE_TOO_BIG_RESERVE_SIZE;
      error_resp.message = "To big reserved size, maximum 255";
      return false;
    }

    cryptonote::account_public_address acc = AUTO_VAL_INIT(acc);

    if(!req.wallet_address.size() || !cryptonote::get_account_address_from_str(acc, req.wallet_address))
    {
      error_resp.code = CORE_RPC_ERROR_CODE_WRONG_WALLET_ADDRESS;
      error_resp.message = "Failed to parse wallet address";
      return false;
    }

    block b = AUTO_VAL_INIT(b);
    cryptonote::blobdata blob_reserve;
    blob_reserve.resize(req.reserve_size, 0);
    if(!m_core.get_block_template(b, acc, res.difficulty, res.height, blob_reserve))
    {
      error_resp.code = CORE_RPC_ERROR_CODE_INTERNAL_ERROR;
      error_resp.message = "Internal error: failed to create block template";
      LOG_ERROR("Failed to create block template");
      return false;
    }
    blobdata block_blob = t_serializable_object_to_blob(b);
    crypto::public_key tx_pub_key = null_pkey;
    cryptonote::parse_and_validate_tx_extra(b.miner_tx, tx_pub_key);
    if(tx_pub_key == null_pkey)
    {
      error_resp.code = CORE_RPC_ERROR_CODE_INTERNAL_ERROR;
      error_resp.message = "Internal error: failed to create block template";
      LOG_ERROR("Failed to  tx pub key in coinbase extra");
      return false;
    }
    res.reserved_offset = slow_memmem((void*)block_blob.data(), block_blob.size(), &tx_pub_key, sizeof(tx_pub_key));
    if(!res.reserved_offset)
    {
      error_resp.code = CORE_RPC_ERROR_CODE_INTERNAL_ERROR;
      error_resp.message = "Internal error: failed to create block template";
      LOG_ERROR("Failed to find tx pub key in blockblob");
      return false;
    }
    res.reserved_offset += sizeof(tx_pub_key) + 3; //3 bytes: tag for TX_EXTRA_TAG_PUBKEY(1 byte), tag for TX_EXTRA_NONCE(1 byte), counter in TX_EXTRA_NONCE(1 byte)
    if(res.reserved_offset + req.reserve_size > block_blob.size())
    {
      error_resp.code = CORE_RPC_ERROR_CODE_INTERNAL_ERROR;
      error_resp.message = "Internal error: failed to create block template";
      LOG_ERROR("Failed to calculate offset for ");
      return false;
    }
    res.blocktemplate_blob = string_tools::buff_to_hex_nodelimer(block_blob);

    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------
  bool core_rpc_server::on_submitblock(const COMMAND_RPC_SUBMITBLOCK::request& req, COMMAND_RPC_SUBMITBLOCK::response& res, epee::json_rpc::error& error_resp)
  {
    if(req.size()!=1)
    {
      error_resp.code = CORE_RPC_ERROR_CODE_WRONG_PARAM;
      error_resp.message = "Wrong param";
      return false;
    }
    blobdata blockblob;
    if(!string_tools::parse_hexstr_to_binbuff(req[0], blockblob))
    {
      error_resp.code = CORE_RPC_ERROR_CODE_WRONG_BLOCKBLOB;
      error_resp.message = "Wrong block blob";
      return false;
    }
    cryptonote::block_verification_context bvc = AUTO_VAL_INIT(bvc);
    m_core.handle_incoming_block(blockblob, bvc);
    if(!bvc.m_added_to_main_chain)
    {
      error_resp.code = CORE_RPC_ERROR_CODE_BLOCK_NOT_ACCEPTED;
      error_resp.message = "Block not accepted";
      return false;
    }
    res.status = "OK";
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------------


}
