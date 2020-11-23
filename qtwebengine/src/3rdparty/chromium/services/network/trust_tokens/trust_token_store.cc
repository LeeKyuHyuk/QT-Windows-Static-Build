// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/trust_tokens/trust_token_store.h"

#include <memory>
#include <utility>

#include "base/optional.h"
#include "services/network/public/mojom/trust_tokens.mojom-forward.h"
#include "services/network/trust_tokens/in_memory_trust_token_persister.h"
#include "services/network/trust_tokens/proto/public.pb.h"
#include "services/network/trust_tokens/proto/storage.pb.h"
#include "services/network/trust_tokens/trust_token_parameterization.h"
#include "services/network/trust_tokens/types.h"
#include "third_party/protobuf/src/google/protobuf/repeated_field.h"

namespace network {

namespace {
// Until the underlying BoringSSL functionality is implemented to extract
// expiry timestamps from Signed Redemption Record bodies, default to
// never expiring stored SRRs.
class NeverExpiringExpiryDelegate
    : public TrustTokenStore::RecordExpiryDelegate {
 public:
  bool IsRecordExpired(
      const SignedTrustTokenRedemptionRecord& record) override {
    return false;
  }
};
}  // namespace

TrustTokenStore::TrustTokenStore(std::unique_ptr<TrustTokenPersister> persister)
    : TrustTokenStore(std::move(persister),
                      std::make_unique<NeverExpiringExpiryDelegate>()) {}

TrustTokenStore::TrustTokenStore(
    std::unique_ptr<TrustTokenPersister> persister,
    std::unique_ptr<RecordExpiryDelegate> expiry_delegate_for_testing)
    : persister_(std::move(persister)),
      record_expiry_delegate_(std::move(expiry_delegate_for_testing)) {
  DCHECK(persister_);
}

TrustTokenStore::~TrustTokenStore() = default;

std::unique_ptr<TrustTokenStore> TrustTokenStore::CreateInMemory() {
  return std::make_unique<TrustTokenStore>(
      std::make_unique<InMemoryTrustTokenPersister>());
}

void TrustTokenStore::RecordIssuance(const url::Origin& issuer) {
  DCHECK(!issuer.opaque());
  url::Origin issuer_origin = issuer;
  std::unique_ptr<TrustTokenIssuerConfig> config =
      persister_->GetIssuerConfig(issuer);
  if (!config)
    config = std::make_unique<TrustTokenIssuerConfig>();
  config->set_last_issuance(internal::TimeToString(base::Time::Now()));
  persister_->SetIssuerConfig(issuer, std::move(config));
}

base::Optional<base::TimeDelta> TrustTokenStore::TimeSinceLastIssuance(
    const url::Origin& issuer) {
  DCHECK(!issuer.opaque());
  std::unique_ptr<TrustTokenIssuerConfig> config =
      persister_->GetIssuerConfig(issuer);
  if (!config)
    return base::nullopt;
  if (!config->has_last_issuance())
    return base::nullopt;
  base::Optional<base::Time> maybe_last_issuance =
      internal::StringToTime(config->last_issuance());
  if (!maybe_last_issuance)
    return base::nullopt;

  base::TimeDelta ret = base::Time::Now() - *maybe_last_issuance;
  if (ret < base::TimeDelta())
    return base::nullopt;

  return ret;
}

void TrustTokenStore::RecordRedemption(const url::Origin& issuer,
                                       const url::Origin& top_level) {
  DCHECK(!issuer.opaque());
  DCHECK(!top_level.opaque());
  std::unique_ptr<TrustTokenIssuerToplevelPairConfig> config =
      persister_->GetIssuerToplevelPairConfig(issuer, top_level);
  if (!config)
    config = std::make_unique<TrustTokenIssuerToplevelPairConfig>();
  config->set_last_redemption(internal::TimeToString(base::Time::Now()));
  persister_->SetIssuerToplevelPairConfig(issuer, top_level, std::move(config));
}

base::Optional<base::TimeDelta> TrustTokenStore::TimeSinceLastRedemption(
    const url::Origin& issuer,
    const url::Origin& top_level) {
  DCHECK(!issuer.opaque());
  DCHECK(!top_level.opaque());
  auto config = persister_->GetIssuerToplevelPairConfig(issuer, top_level);
  if (!config)
    return base::nullopt;
  if (!config->has_last_redemption())
    return base::nullopt;
  base::Optional<base::Time> maybe_last_redemption =
      internal::StringToTime(config->last_redemption());
  // internal::StringToTime can fail in the case of data corruption (or writer
  // error).
  if (!maybe_last_redemption)
    return base::nullopt;

  base::TimeDelta ret = base::Time::Now() - *maybe_last_redemption;
  if (ret < base::TimeDelta())
    return base::nullopt;
  return ret;
}

bool TrustTokenStore::IsAssociated(const url::Origin& issuer,
                                   const url::Origin& top_level) {
  DCHECK(!issuer.opaque());
  DCHECK(!top_level.opaque());
  std::unique_ptr<TrustTokenToplevelConfig> config =
      persister_->GetToplevelConfig(top_level);
  if (!config)
    return false;
  return base::Contains(config->associated_issuers(), issuer.Serialize());
}

bool TrustTokenStore::SetAssociation(const url::Origin& issuer,
                                     const url::Origin& top_level) {
  DCHECK(!issuer.opaque());
  DCHECK(!top_level.opaque());
  std::unique_ptr<TrustTokenToplevelConfig> config =
      persister_->GetToplevelConfig(top_level);
  if (!config)
    config = std::make_unique<TrustTokenToplevelConfig>();
  auto string_issuer = issuer.Serialize();

  if (base::Contains(config->associated_issuers(), string_issuer))
    return true;

  if (config->associated_issuers_size() >=
      kTrustTokenPerToplevelMaxNumberOfAssociatedIssuers) {
    return false;
  }

  config->add_associated_issuers(std::move(string_issuer));
  persister_->SetToplevelConfig(top_level, std::move(config));

  return true;
}

void TrustTokenStore::PruneStaleIssuerState(
    const url::Origin& issuer,
    const std::vector<mojom::TrustTokenVerificationKeyPtr>& keys) {
  DCHECK(!issuer.opaque());
  DCHECK([&keys]() {
    std::set<base::StringPiece> unique_keys;
    for (const auto& key : keys)
      unique_keys.insert(base::StringPiece(key->body));
    return unique_keys.size() == keys.size();
  }());

  std::unique_ptr<TrustTokenIssuerConfig> config =
      persister_->GetIssuerConfig(issuer);
  if (!config)
    config = std::make_unique<TrustTokenIssuerConfig>();

  google::protobuf::RepeatedPtrField<TrustToken> filtered_tokens;
  for (auto& token : *config->mutable_tokens()) {
    if (std::any_of(keys.begin(), keys.end(),
                    [&token](const mojom::TrustTokenVerificationKeyPtr& key) {
                      return key->body == token.signing_key();
                    }))
      *filtered_tokens.Add() = std::move(token);
  }

  config->mutable_tokens()->Swap(&filtered_tokens);

  persister_->SetIssuerConfig(issuer, std::move(config));
}

void TrustTokenStore::AddTokens(const url::Origin& issuer,
                                base::span<const std::string> token_bodies,
                                base::StringPiece issuing_key) {
  DCHECK(!issuer.opaque());
  auto config = persister_->GetIssuerConfig(issuer);
  if (!config)
    config = std::make_unique<TrustTokenIssuerConfig>();

  for (auto it = token_bodies.begin();
       it != token_bodies.end() &&
       config->tokens_size() < kTrustTokenPerIssuerTokenCapacity;
       ++it) {
    TrustToken* entry = config->add_tokens();
    entry->set_body(*it);
    entry->set_signing_key(std::string(issuing_key));
  }

  persister_->SetIssuerConfig(issuer, std::move(config));
}

int TrustTokenStore::CountTokens(const url::Origin& issuer) {
  DCHECK(!issuer.opaque());
  auto config = persister_->GetIssuerConfig(issuer);
  if (!config)
    return 0;
  return config->tokens_size();
}

std::vector<TrustToken> TrustTokenStore::RetrieveMatchingTokens(
    const url::Origin& issuer,
    base::RepeatingCallback<bool(const std::string&)> key_matcher) {
  DCHECK(!issuer.opaque());
  auto config = persister_->GetIssuerConfig(issuer);
  std::vector<TrustToken> matching_tokens;
  if (!config)
    return matching_tokens;

  std::copy_if(config->tokens().begin(), config->tokens().end(),
               std::back_inserter(matching_tokens),
               [&key_matcher](const TrustToken& token) {
                 return token.has_signing_key() &&
                        key_matcher.Run(token.signing_key());
               });

  return matching_tokens;
}

void TrustTokenStore::DeleteToken(const url::Origin& issuer,
                                  const TrustToken& to_delete) {
  DCHECK(!issuer.opaque());
  auto config = persister_->GetIssuerConfig(issuer);
  if (!config)
    return;

  for (auto it = config->mutable_tokens()->begin();
       it != config->mutable_tokens()->end(); ++it) {
    if (it->body() == to_delete.body()) {
      config->mutable_tokens()->erase(it);
      break;
    }
  }

  persister_->SetIssuerConfig(issuer, std::move(config));
}

void TrustTokenStore::SetRedemptionRecord(
    const url::Origin& issuer,
    const url::Origin& top_level,
    const SignedTrustTokenRedemptionRecord& record) {
  DCHECK(!issuer.opaque());
  DCHECK(!top_level.opaque());
  auto config = persister_->GetIssuerToplevelPairConfig(issuer, top_level);
  if (!config)
    config = std::make_unique<TrustTokenIssuerToplevelPairConfig>();
  *config->mutable_signed_redemption_record() = record;
  persister_->SetIssuerToplevelPairConfig(issuer, top_level, std::move(config));
}

base::Optional<SignedTrustTokenRedemptionRecord>
TrustTokenStore::RetrieveNonstaleRedemptionRecord(
    const url::Origin& issuer,
    const url::Origin& top_level) {
  DCHECK(!issuer.opaque());
  DCHECK(!top_level.opaque());
  auto config = persister_->GetIssuerToplevelPairConfig(issuer, top_level);
  if (!config)
    return base::nullopt;

  if (!config->has_signed_redemption_record())
    return base::nullopt;

  if (record_expiry_delegate_->IsRecordExpired(
          config->signed_redemption_record()))
    return base::nullopt;

  return config->signed_redemption_record();
}

}  // namespace network
