// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_TRUST_TOKENS_TRUST_TOKEN_STORE_H_
#define SERVICES_NETWORK_TRUST_TOKENS_TRUST_TOKEN_STORE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/optional.h"
#include "base/sequenced_task_runner.h"
#include "base/time/time.h"
#include "services/network/public/mojom/trust_tokens.mojom.h"
#include "services/network/trust_tokens/proto/public.pb.h"
#include "services/network/trust_tokens/trust_token_persister.h"
#include "services/network/trust_tokens/types.h"
#include "url/origin.h"

namespace network {

// A TrustTokenStore provides operations on persistent state necessary for
// the various steps of the Trust TrustTokens protocol.
//
// For more information about the protocol, see the explainer at
// https://github.com/WICG/trust-token-api.
//
// TrustTokenStore translates operations germane to different steps
// of token issuance, token redemption, and request signing into
// operations in the key-value representation used by the persistence
// layer.
//
// For example, it provides operations:
// - checking preconditions for the different protocol steps;
// - storing unblinded, signed tokens; and
// - managing Signed Redemption Records (SRRs) and corresponding key pairs.
//
// TrustTokenStore's methods do minimal precondition checking and, in
// particular, only selectively verify protocol-level invariants and
// input integrity.
class TrustTokenStore {
 public:
  class RecordExpiryDelegate {
   public:
    virtual ~RecordExpiryDelegate() = default;

    // Returns whether the given Signed Redemption Record has expired.
    // This is implemented with a delegate to abstract away reading
    // the values of SRRs (they're opaque to this store).
    virtual bool IsRecordExpired(
        const SignedTrustTokenRedemptionRecord& record) = 0;
  };

  // Creates a new TrustTokenStore passing read and write operations through
  // to the given persister.
  //
  // Until the underlying BoringSSL functionality is implemented to extract
  // expiry timestamps from Signed Redemption Record bodies, defaults to
  // never expiring stored SRRs.
  //
  // |persister| must not be null.
  explicit TrustTokenStore(std::unique_ptr<TrustTokenPersister> persister);

  // Creates a TrustTokenStore relying on the given delegate for judging whether
  // signed redemption records have expired.
  //
  // |persister| must not be null.
  TrustTokenStore(
      std::unique_ptr<TrustTokenPersister> persister,
      std::unique_ptr<RecordExpiryDelegate> expiry_delegate_for_testing);

  virtual ~TrustTokenStore();

  // Creates a TrustTokenStore on top of an in-memory persister.
  static std::unique_ptr<TrustTokenStore> CreateInMemory();

  //// Methods related to ratelimits:

  // Updates the given issuer's last issuance time to now.
  //
  // |issuer| must not be opaque.
  virtual void RecordIssuance(const url::Origin& issuer);

  // Returns the time since the last call to RecordIssuance for
  // issuer |issuer|, or nullopt in the following two cases:
  // 1. there is no currently-recorded prior issuance for the
  // issuer, or
  // 2. the time since the last issuance is negative (because
  // of, for instance, corruption or clock skew).
  //
  // |issuer| must not be opaque.
  WARN_UNUSED_RESULT virtual base::Optional<base::TimeDelta>
  TimeSinceLastIssuance(const url::Origin& issuer);

  // Updates the given (issuer, top-level) origin pair's last redemption time
  // to now.
  //
  // |issuer| and |top_level| must not be opaque.
  virtual void RecordRedemption(const url::Origin& issuer,
                                const url::Origin& top_level);

  // Returns the time elapsed since the last redemption recorded by
  // RecordRedemption for issuer |issuer| and top level |top_level|,
  // or nullopt in the following two cases:
  // 1. there was no prior redemption for the (issuer,
  // top-level origin) pair.
  // 2. the time since the last redepmption is negative (because
  // of, for instance, corruption or clock skew).
  //
  // |issuer| and |top_level| must not be opaque.
  WARN_UNUSED_RESULT virtual base::Optional<base::TimeDelta>
  TimeSinceLastRedemption(const url::Origin& issuer,
                          const url::Origin& top_level);

  // Returns whether |issuer| is associated with |top_level|.
  //
  // |issuer| and |top_level| must not be opaque.
  WARN_UNUSED_RESULT virtual bool IsAssociated(const url::Origin& issuer,
                                               const url::Origin& top_level);

  // If associating |issuer| with |top_level| would exceed the cap on the number
  // of issuers allowed to be associated with a given top-level origin, returns
  // false. Otherwise, associates |issuer| with |top_level| and returns true.
  //
  // TODO(crbug.com/1060716): As part of adding solid support for multiple
  // issuers, it'd be good to make these associations expire after some
  // reasonably long amount of time, so that top-level origins can change their
  // minds about their associated issuers.
  //
  // |issuer| and |top_level| must not be opaque.
  WARN_UNUSED_RESULT virtual bool SetAssociation(const url::Origin& issuer,
                                                 const url::Origin& top_level);

  //// Methods related to reading and writing issuer values configured via key
  //// commitment queries, such as key commitments and batch sizes:

  // Given an issuer's current set |keys| of key commitments, prunes all state
  // for |issuer| that does *not* correspond to token verification keys in
  // |keys|:
  // - removes all stored signed tokens for |issuer| that were signed with
  // keys not in |keys|
  //
  // |issuer| must not be opaque, and the commitments in |keys| must have
  // distinct keys.
  virtual void PruneStaleIssuerState(
      const url::Origin& issuer,
      const std::vector<mojom::TrustTokenVerificationKeyPtr>& keys);

  //// Methods related to reading and writing signed tokens:

  // Associates to the given issuer additional signed
  // trust tokens with:
  // - token bodies given by |token_bodies|
  // - signing keys given by |issuing_key|.
  //
  // Note: This method makes no assumption about tokens matching an issuer's
  // current key commitments; it's the caller's responsibility to avoid using
  // tokens issued against non-current keys.
  //
  // |issuer| must not be opaque.
  virtual void AddTokens(const url::Origin& issuer,
                         base::span<const std::string> token_bodies,
                         base::StringPiece issuing_key);

  // Returns the number of tokens stored for |issuer|.
  //
  // |issuer| must not be opaque.
  WARN_UNUSED_RESULT virtual int CountTokens(const url::Origin& issuer);

  // Returns all signed tokens from |issuer| signed by keys matching
  // the given predicate.
  //
  // |issuer| must not be opaque.
  WARN_UNUSED_RESULT virtual std::vector<TrustToken> RetrieveMatchingTokens(
      const url::Origin& issuer,
      base::RepeatingCallback<bool(const std::string&)> key_matcher);

  // If |to_delete| is a currently stored token issued by |issuer|, deletes the
  // token.
  //
  // |issuer| must not be opaque.
  void DeleteToken(const url::Origin& issuer, const TrustToken& to_delete);

  //// Methods concerning Signed Redemption Records (SRRs)

  // Sets the cached SRR corresponding to the pair (issuer, top_level)
  // to |record|. Overwrites any existing record.
  //
  // |issuer| and |top_level| must not be opaque.
  virtual void SetRedemptionRecord(
      const url::Origin& issuer,
      const url::Origin& top_level,
      const SignedTrustTokenRedemptionRecord& record);

  // Attempts to retrieve the stored SRR for the given pair of (issuer,
  // top-level) origins.
  // - If the pair has a current (i.e., non-expired) SRR, returns that SRR.
  // - Otherwise, returns nullopt.
  //
  // |issuer| and |top_level| must not be opaque.
  WARN_UNUSED_RESULT virtual base::Optional<SignedTrustTokenRedemptionRecord>
  RetrieveNonstaleRedemptionRecord(const url::Origin& issuer,
                                   const url::Origin& top_level);

 private:
  std::unique_ptr<TrustTokenPersister> persister_;
  std::unique_ptr<RecordExpiryDelegate> record_expiry_delegate_;
};

}  // namespace network

#endif  // SERVICES_NETWORK_TRUST_TOKENS_TRUST_TOKEN_STORE_H_
