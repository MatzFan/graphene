/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once
#include <graphene/chain/protocol/operations.hpp>
#include <graphene/db/generic_index.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace graphene { namespace chain {
   class database;

   /**
    * @class account_statistics_object
    * @ingroup object
    * @ingroup implementation
    *
    * This object contains regularly updated statistical data about an account. It is provided for the purpose of
    * separating the account data that changes frequently from the account data that is mostly static, which will
    * minimize the amount of data that must be backed up as part of the undo history everytime a transfer is made.
    */
   class account_statistics_object : public graphene::db::abstract_object<account_statistics_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_account_statistics_object_type;

         account_id_type  owner;

         /**
          * Keep the most recent operation as a root pointer to a linked list of the transaction history. This field is
          * not required by core validation and could in theory be made an annotation on the account object, but
          * because transaction history is so common and this object is already cached in the undo buffer (because it
          * likely affected the balances of this account) it is convienent to simply track this data here. Account
          * balance objects don't currenty inherit from annotated object.
          */
         account_transaction_history_id_type most_recent_op;

         /**
          * When calculating votes it is necessary to know how much is stored in orders (and thus unavailable for
          * transfers). Rather than maintaining an index of [asset,owner,order_id] we will simply maintain the running
          * total here and update it every time an order is created or modified.
          */
         share_type total_core_in_orders;

         /**
          * Tracks the total fees paid by this account for the purpose of calculating bulk discounts.
          */
         share_type lifetime_fees_paid;

         /**
          * Tracks the fees paid by this account which have not been disseminated to the various parties that receive
          * them yet (registrar, referrer, lifetime referrer, network, etc). This is used as an optimization to avoid
          * doing massive amounts of uint128 arithmetic on each and every operation.
          *
          * These fees will be paid out as vesting cash-back, and this counter will reset during the maintenance
          * interval.
          */
         share_type pending_fees;
         /**
          * Same as @ref pending_fees, except these fees will be paid out as pre-vested cash-back (immediately
          * available for withdrawal) rather than requiring the normal vesting period.
          */
         share_type pending_vested_fees;

         /// @brief Split up and pay out @ref pending_fees and @ref pending_vested_fees
         void process_fees(const account_object& a, database& d) const;
   };

   /**
    * @brief Tracks the balance of a single account/asset pair
    * @ingroup object
    *
    * This object is indexed on owner and asset_type so that black swan
    * events in asset_type can be processed quickly.
    */
   class account_balance_object : public abstract_object<account_balance_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_account_balance_object_type;

         account_id_type   owner;
         asset_id_type     asset_type;
         share_type        balance;

         asset get_balance()const { return asset(balance, asset_type); }
         void  adjust_balance(const asset& delta);
   };


   /**
    * @brief This class represents an account on the object graph
    * @ingroup object
    * @ingroup protocol
    *
    * Accounts are the primary unit of authority on the graphene system. Users must have an account in order to use
    * assets, trade in the markets, vote for committee_members, etc.
    */
   class account_object : public graphene::db::annotated_object<account_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = account_object_type;

         /**
          * The time at which this account's membership expires.
          * If set to any time in the past, the account is a basic account.
          * If set to time_point_sec::maximum(), the account is a lifetime member.
          * If set to any time not in the past less than time_point_sec::maximum(), the account is an annual member.
          *
          * See @ref is_lifetime_member, @ref is_basic_account, @ref is_annual_member, and @ref is_member
          */
         time_point_sec membership_expiration_date;

         ///The account that paid the fee to register this account. Receives a percentage of referral rewards.
         account_id_type registrar;
         /// The account credited as referring this account. Receives a percentage of referral rewards.
         account_id_type referrer;
         /// The lifetime member at the top of the referral tree. Receives a percentage of referral rewards.
         account_id_type lifetime_referrer;

         /// Percentage of fee which should go to network.
         uint16_t network_fee_percentage;
         /// Percentage of fee which should go to lifetime referrer.
         uint16_t lifetime_referrer_fee_percentage = 0;
         /// Percentage of referral rewards (leftover fee after paying network and lifetime referrer) which should go
         /// to referrer. The remainder of referral rewards goes to the registrar.
         uint16_t referrer_rewards_percentage = 0;

         /// The account's name. This name must be unique among all account names on the graph. May not be empty.
         string name;

         /**
          * The owner authority represents absolute control over the account. Usually the keys in this authority will
          * be kept in cold storage, as they should not be needed very often and compromise of these keys constitutes
          * complete and irrevocable loss of the account. Generally the only time the owner authority is required is to
          * update the active authority.
          */
         authority owner;
         /// The owner authority contains the hot keys of the account. This authority has control over nearly all
         /// operations the account may perform.
         authority active;

         typedef account_options  options_type;
         account_options options;

         /// The reference implementation records the account's statistics in a separate object. This field contains the
         /// ID of that object.
         account_statistics_id_type statistics;

         /**
          * This is a set of all accounts which have 'whitelisted' this account. Whitelisting is only used in core
          * validation for the purpose of authorizing accounts to hold and transact in whitelisted assets. This
          * account cannot update this set, except by transferring ownership of the account, which will clear it. Other
          * accounts may add or remove their IDs from this set.
          */
         flat_set<account_id_type> whitelisting_accounts;

         /**
          * This is a set of all accounts which have 'blacklisted' this account. Blacklisting is only used in core
          * validation for the purpose of forbidding accounts from holding and transacting in whitelisted assets. This
          * account cannot update this set, and it will be preserved even if the account is transferred. Other accounts
          * may add or remove their IDs from this set.
          */
         flat_set<account_id_type> blacklisting_accounts;

         /**
          * Vesting balance which receives cashback_reward deposits.
          */
         optional<vesting_balance_id_type> cashback_vb;
         template<typename DB>
         const vesting_balance_object& cashback_balance(const DB& db)const
         {
            FC_ASSERT(cashback_vb);
            return db.get(*cashback_vb);
         }

         /// @return true if this is a lifetime member account; false otherwise.
         bool is_lifetime_member()const
         {
            return membership_expiration_date == time_point_sec::maximum();
         }
         /// @return true if this is a basic account; false otherwise.
         bool is_basic_account(time_point_sec now)const
         {
            return now > membership_expiration_date;
         }
         /// @return true if the account is an unexpired annual member; false otherwise.
         /// @note This method will return false for lifetime members.
         bool is_annual_member(time_point_sec now)const
         {
            return !is_lifetime_member() && !is_basic_account(now);
         }
         /// @return true if the account is an annual or lifetime member; false otherwise.
         bool is_member(time_point_sec now)const
         {
            return !is_basic_account(now);
         }

         /**
          * @return true if this account is whitelisted and not blacklisted to transact in the provided asset; false
          * otherwise.
          */
         bool is_authorized_asset(const asset_object& asset_obj)const;

         account_id_type get_id()const { return id; }
   };

   /**
    *  This object is attached as the meta annotation on the account object, this information is not relevant to
    *  validation.
    */
   class meta_account_object : public graphene::db::abstract_object<meta_account_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = meta_account_object_type;

         public_key_type     memo_key;
         committee_member_id_type    committee_member_id; // optional
   };

   /**
    *  @brief This secondary index will allow a reverse lookup of all accounts that a particular key or account
    *  is an potential signing authority.
    */
   class account_member_index : public secondary_index
   {
      public:
         virtual void object_inserted( const object& obj ) override;
         virtual void object_removed( const object& obj ) override;
         virtual void about_to_modify( const object& before ) override;
         virtual void object_modified( const object& after  ) override;


         /** given an account or key, map it to the set of accounts that reference it in an active or owner authority */
         map< account_id_type, set<account_id_type> > account_to_account_memberships;
         map< public_key_type, set<account_id_type> > account_to_key_memberships;
         /** some accounts use address authorities in the genesis block */
         map< address, set<account_id_type> >         account_to_address_memberships;


      protected:
         set<account_id_type>  get_account_members( const account_object& a )const;
         set<public_key_type>  get_key_members( const account_object& a )const;
         set<address>          get_address_members( const account_object& a )const;

         set<account_id_type>  before_account_members;
         set<public_key_type>  before_key_members;
         set<address>          before_address_members;
   };

   /**
    *  @brief This secondary index will allow a reverse lookup of all accounts that have been referred by
    *  a particular account.
    */
   class account_referrer_index : public secondary_index
   {
      public:
         virtual void object_inserted( const object& obj ) override;
         virtual void object_removed( const object& obj ) override;
         virtual void about_to_modify( const object& before ) override;
         virtual void object_modified( const object& after  ) override;

         /** maps the referrer to the set of accounts that they have referred */
         map< account_id_type, set<account_id_type> > referred_by;
   };

   struct by_asset;
   struct by_account;
   struct by_balance;
   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      account_balance_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         hashed_unique< tag<by_balance>, composite_key<
            account_balance_object,
            member<account_balance_object, account_id_type, &account_balance_object::owner>,
            member<account_balance_object, asset_id_type, &account_balance_object::asset_type> >
         >,
         ordered_non_unique< tag<by_account>, member<account_balance_object, account_id_type, &account_balance_object::owner> >,
         ordered_non_unique< tag<by_asset>, member<account_balance_object, asset_id_type, &account_balance_object::asset_type> >
      >
   > account_balance_object_multi_index_type;

   /**
    * @ingroup object_index
    */
   typedef generic_index<account_balance_object, account_balance_object_multi_index_type> account_balance_index;

   struct by_name{};

   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      account_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_unique< tag<by_name>, member<account_object, string, &account_object::name> >
      >
   > account_multi_index_type;

   /**
    * @ingroup object_index
    */
   typedef generic_index<account_object, account_multi_index_type> account_index;

}}

FC_REFLECT_DERIVED( graphene::chain::account_object,
                    (graphene::db::annotated_object<graphene::chain::account_object>),
                    (membership_expiration_date)(registrar)(referrer)(lifetime_referrer)
                    (network_fee_percentage)(lifetime_referrer_fee_percentage)(referrer_rewards_percentage)
                    (name)(owner)(active)(options)(statistics)(whitelisting_accounts)(blacklisting_accounts)
                    (cashback_vb) )

FC_REFLECT_DERIVED( graphene::chain::account_balance_object,
                    (graphene::db::object),
                    (owner)(asset_type)(balance) )

FC_REFLECT_DERIVED( graphene::chain::meta_account_object,
                    (graphene::db::object),
                    (memo_key)(committee_member_id) )

FC_REFLECT_DERIVED( graphene::chain::account_statistics_object, (graphene::chain::object),
                    (owner)
                    (most_recent_op)
                    (total_core_in_orders)
                    (lifetime_fees_paid)
                    (pending_fees)(pending_vested_fees)
                  )

