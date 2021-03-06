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

#include <graphene/chain/protocol/asset.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>

#include <fc/static_variant.hpp>
#include <fc/uint128.hpp>

#include <algorithm>



namespace graphene { namespace chain {
   using namespace graphene::db;

   class vesting_balance_object;

   struct vesting_policy_context
   {
      vesting_policy_context(
         asset _balance,
         fc::time_point_sec _now,
         asset _amount)
         : balance(_balance), now(_now), amount(_amount) {}

      asset              balance;
      fc::time_point_sec now;
      asset              amount;
   };

   /**
    * @brief Linear vesting balance with cliff
    *
    * This vesting balance type is used to mimic traditional stock vesting contracts where
    * each day a certain amount vests until it is fully matured.
    *
    * @note New funds may not be added to a linear vesting balance.
    */
   struct linear_vesting_policy
   {
      /// This is the time at which funds begin vesting.
      fc::time_point_sec begin_timestamp;
      /// No amount may be withdrawn before this many seconds of the vesting period have elapsed.
      uint32_t vesting_cliff_seconds = 0;
      /// Duration of the vesting period, in seconds. Must be greater than 0 and greater than vesting_cliff_seconds.
      uint32_t vesting_duration_seconds = 0;
      /// The total amount of asset to vest.
      share_type begin_balance;

      asset get_allowed_withdraw(const vesting_policy_context& ctx)const;
      bool is_deposit_allowed(const vesting_policy_context& ctx)const;
      bool is_deposit_vested_allowed(const vesting_policy_context&)const { return false; }
      bool is_withdraw_allowed(const vesting_policy_context& ctx)const;
      void on_deposit(const vesting_policy_context& ctx);
      void on_deposit_vested(const vesting_policy_context&)
      { FC_THROW( "May not deposit vested into a linear vesting balance." ); }
      void on_withdraw(const vesting_policy_context& ctx);
   };

   /**
    * @brief defines vesting in terms of coin-days accrued which allows for dynamic deposit/withdraw
    *
    * The economic effect of this vesting policy is to require a certain amount of "interest" to accrue
    * before the full balance may be withdrawn.  Interest accrues as coindays (balance * length held).  If
    * some of the balance is withdrawn, the remaining balance must be held longer.
    */
   struct cdd_vesting_policy
   {
      uint32_t                       vesting_seconds = 0;
      fc::uint128_t                  coin_seconds_earned;
      /** while coindays may accrue over time, none may be claimed before first_claim date */
      fc::time_point_sec             start_claim;
      fc::time_point_sec             coin_seconds_earned_last_update;

      /**
       * Compute coin_seconds_earned.  Used to
       * non-destructively figure out how many coin seconds
       * are available.
       */
      fc::uint128_t compute_coin_seconds_earned(const vesting_policy_context& ctx)const;

      /**
       * Update coin_seconds_earned and
       * coin_seconds_earned_last_update fields; called by both
       * on_deposit() and on_withdraw().
       */
      void update_coin_seconds_earned(const vesting_policy_context& ctx);

      asset get_allowed_withdraw(const vesting_policy_context& ctx)const;
      bool is_deposit_allowed(const vesting_policy_context& ctx)const;
      bool is_deposit_vested_allowed(const vesting_policy_context& ctx)const;
      bool is_withdraw_allowed(const vesting_policy_context& ctx)const;
      void on_deposit(const vesting_policy_context& ctx);
      void on_deposit_vested(const vesting_policy_context& ctx);
      void on_withdraw(const vesting_policy_context& ctx);
   };

   typedef fc::static_variant<
      linear_vesting_policy,
      cdd_vesting_policy
      > vesting_policy;

   /**
    * Vesting balance object is a balance that is locked by the blockchain for a period of time.
    */
   class vesting_balance_object : public abstract_object<vesting_balance_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id = vesting_balance_object_type;

         /// Account which owns and may withdraw from this vesting balance
         account_id_type owner;
         /// Total amount remaining in this vesting balance
         /// Includes the unvested funds, and the vested funds which have not yet been withdrawn
         asset balance;
         /// The vesting policy stores details on when funds vest, and controls when they may be withdrawn
         vesting_policy policy;

         vesting_balance_object() {}

         ///@brief Deposit amount into vesting balance, requiring it to vest before withdrawal
         void deposit(const fc::time_point_sec& now, const asset& amount);
         bool is_deposit_allowed(const fc::time_point_sec& now, const asset& amount)const;

         /// @brief Deposit amount into vesting balance, making the new funds vest immediately
         void deposit_vested(const fc::time_point_sec& now, const asset& amount);
         bool is_deposit_vested_allowed(const fc::time_point_sec& now, const asset& amount)const;

         /**
          * Used to remove a vesting balance from the VBO. As well as the
          * balance field, coin_seconds_earned and
          * coin_seconds_earned_last_update fields are updated.
          *
          * The money doesn't "go" anywhere; the caller is responsible for
          * crediting it to the proper account.
          */
         void withdraw(const fc::time_point_sec& now, const asset& amount);
         bool is_withdraw_allowed(const fc::time_point_sec& now, const asset& amount)const;
   };
   /**
    * @ingroup object_index
    */
   struct by_account;
   typedef multi_index_container<
      vesting_balance_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_account>,
            member<vesting_balance_object, account_id_type, &vesting_balance_object::owner>
         >
      >
   > vesting_balance_multi_index_type;
   /**
    * @ingroup object_index
    */
   typedef generic_index<vesting_balance_object, vesting_balance_multi_index_type> vesting_balance_index;

} } // graphene::chain

FC_REFLECT(graphene::chain::linear_vesting_policy,
           (begin_timestamp)
           (vesting_cliff_seconds)
           (vesting_duration_seconds)
           (begin_balance)
          )

FC_REFLECT(graphene::chain::cdd_vesting_policy,
           (vesting_seconds)
           (start_claim)
           (coin_seconds_earned)
           (coin_seconds_earned_last_update)
          )

FC_REFLECT_TYPENAME( graphene::chain::vesting_policy )

FC_REFLECT_DERIVED(graphene::chain::vesting_balance_object, (graphene::db::object),
                   (owner)
                   (balance)
                   (policy)
                  )
