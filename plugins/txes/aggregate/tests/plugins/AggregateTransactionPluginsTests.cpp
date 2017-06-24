#include "src/plugins/AggregateTransactionPlugins.h"
#include "src/model/AggregateNotifications.h"
#include "src/model/AggregateTransaction.h"
#include "tests/test/core/mocks/MockNotificationSubscriber.h"
#include "tests/test/core/mocks/MockTransaction.h"
#include "tests/test/plugins/TransactionPluginTestUtils.h"
#include "tests/TestHarness.h"

using namespace catapult::model;

namespace catapult { namespace plugins {

#define TEST_CLASS AggregateTransactionPluginsTests

	// region AggregateTransactionWrapper

	namespace {
		struct AggregateTransactionWrapper {
			std::unique_ptr<AggregateTransaction> pTransaction;
			std::vector<const mocks::EmbeddedMockTransaction*> SubTransactions;
			std::vector<Key> SubTransactionSigners;
			std::vector<Key> SubTransactionRecipients;
			std::vector<Key> Cosigners;
			std::vector<Signature> CosignerSignatures;
		};

		AggregateTransactionWrapper CreateAggregateTransaction(uint8_t numTransactions, uint8_t numCosignatures) {
			using TransactionType = AggregateTransaction;
			uint32_t entitySize = sizeof(TransactionType)
					+ numTransactions * sizeof(mocks::EmbeddedMockTransaction)
					+ numCosignatures * sizeof(Cosignature);

			AggregateTransactionWrapper wrapper;
			std::unique_ptr<TransactionType> pTransaction(reinterpret_cast<TransactionType*>(::operator new(entitySize)));
			pTransaction->Size = entitySize;
			pTransaction->PayloadSize = numTransactions * sizeof(mocks::EmbeddedMockTransaction);
			test::FillWithRandomData(pTransaction->Signer);
			test::FillWithRandomData(pTransaction->Signature);

			auto* pSubTransaction = static_cast<mocks::EmbeddedMockTransaction*>(pTransaction->TransactionsPtr());
			for (auto i = 0u; i < numTransactions; ++i) {
				pSubTransaction->Size = sizeof(mocks::EmbeddedMockTransaction);
				pSubTransaction->Data.Size = 0;
				pSubTransaction->Type = mocks::EmbeddedMockTransaction::Entity_Type;
				test::FillWithRandomData(pSubTransaction->Signer);
				test::FillWithRandomData(pSubTransaction->Recipient);

				wrapper.SubTransactions.push_back(pSubTransaction);
				wrapper.SubTransactionSigners.push_back(pSubTransaction->Signer);
				wrapper.SubTransactionRecipients.push_back(pSubTransaction->Recipient);
				++pSubTransaction;
			}

			auto* pCosignature = pTransaction->CosignaturesPtr();
			for (auto i = 0u; i < numCosignatures; ++i) {
				test::FillWithRandomData(pCosignature->Signer);
				test::FillWithRandomData(pCosignature->Signature);

				wrapper.Cosigners.push_back(pCosignature->Signer);
				wrapper.CosignerSignatures.push_back(pCosignature->Signature);
				++pCosignature;
			}

			wrapper.pTransaction = std::move(pTransaction);
			return wrapper;
		}
	}

	// endregion

	// region basic

	TEST(TEST_CLASS, CanCreatePlugin) {
		// Act:
		TransactionRegistry registry;
		auto pPlugin = CreateAggregateTransactionPlugin(registry);

		// Assert:
		EXPECT_EQ(EntityType::Aggregate, pPlugin->type());
	}

	TEST(TEST_CLASS, PluginDoesNotSupportEmbedding) {
		// Arrange:
		TransactionRegistry registry;
		auto pPlugin = CreateAggregateTransactionPlugin(registry);

		// Act + Assert:
		EXPECT_FALSE(pPlugin->supportsEmbedding());
		EXPECT_THROW(pPlugin->embeddedPlugin(), catapult_runtime_error);
	}

	// endregion

	// region size

	TEST(TEST_CLASS, CanCalculateSizeWhenAllSubTransactionsAreSupported) {
		// Arrange:
		auto pRegistry = mocks::CreateDefaultTransactionRegistry();
		auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
		auto wrapper = CreateAggregateTransaction(3, 4);

		// Act:
		auto realSize = pPlugin->calculateRealSize(*wrapper.pTransaction);

		// Assert:
		EXPECT_EQ(sizeof(AggregateTransaction) + 3 * sizeof(mocks::EmbeddedMockTransaction) + 4 * sizeof(Cosignature), realSize);
	}

	TEST(TEST_CLASS, CannotCalculateSizeWhenAnySubTransactionIsNotSupported) {
		// Arrange:
		TransactionRegistry registry;
		auto pPlugin = CreateAggregateTransactionPlugin(registry);
		auto wrapper = CreateAggregateTransaction(3, 4);

		// Act:
		auto realSize = pPlugin->calculateRealSize(*wrapper.pTransaction);

		// Assert:
		EXPECT_EQ(std::numeric_limits<uint64_t>::max(), realSize);
	}

	// endregion

	// region publish

	// region basic

	TEST(TEST_CLASS, CanRaiseCorrectNumberOfNotificationsFromEmptyAggregate) {
		// Arrange:
		mocks::MockNotificationSubscriber sub;
		auto pRegistry = mocks::CreateDefaultTransactionRegistry();
		auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
		auto wrapper = CreateAggregateTransaction(0, 0);

		// Act:
		pPlugin->publish(*wrapper.pTransaction, sub);

		// Assert:
		// - 1 AggregateCosignaturesNotification
		ASSERT_EQ(1u, sub.numNotifications());

		// - aggregate cosignatures notification must be the first raised notification
		EXPECT_EQ(Aggregate_Cosignatures_Notification, sub.notificationTypes()[0]);
	}

	TEST(TEST_CLASS, CanRaiseCorrectNumberOfNotificationsFromAggregate) {
		// Arrange:
		mocks::MockNotificationSubscriber sub;
		auto pRegistry = mocks::CreateDefaultTransactionRegistry();
		auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
		auto wrapper = CreateAggregateTransaction(2, 3);

		// Act:
		pPlugin->publish(*wrapper.pTransaction, sub);

		// Assert:
		// - 1 AggregateCosignaturesNotification
		// - 3 SignatureNotification (one per cosigner)
		// - 4 AccountPublicKeyNotification (two per embedded-mock; one signer and one recipient each)
		// - 2 AggregateEmbeddedTransactionNotification (one per embedded-mock)
		ASSERT_EQ(1u + 3 + 4 + 2, sub.numNotifications());

		// - aggregate cosignatures notification must be the first raised notification
		EXPECT_EQ(Aggregate_Cosignatures_Notification, sub.notificationTypes()[0]);
	}

	// endregion

	// region account (including nested)

	namespace {
		void AssertContainsAllKeys(const mocks::MockNotificationSubscriber& sub, const AggregateTransactionWrapper& wrapper) {
			auto i = 0u;
			for (const auto& key : wrapper.SubTransactionSigners) {
				EXPECT_TRUE(sub.contains(key)) << "sub transaction signer " << i;
				++i;
			}

			for (const auto& key : wrapper.SubTransactionRecipients) {
				EXPECT_TRUE(sub.contains(key)) << "sub transaction recipient " << i;
				++i;
			}

			// Sanity:
			EXPECT_EQ(i, sub.numKeys());
		}
	}

	TEST(TEST_CLASS, CanRaiseAccountNotificationsFromAggregate) {
		// Arrange:
		mocks::MockNotificationSubscriber sub;
		auto pRegistry = mocks::CreateDefaultTransactionRegistry();
		auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
		auto wrapper = CreateAggregateTransaction(2, 3);

		// Act:
		pPlugin->publish(*wrapper.pTransaction, sub);

		// Assert: 2 sub signer and 2 sub recipient notifications are raised
		EXPECT_EQ(0u, sub.numAddresses());
		EXPECT_EQ(4u, sub.numKeys());
		AssertContainsAllKeys(sub, wrapper);
	}

	// endregion

	// region aggregate embedded transaction

	namespace {
		void AssertCanRaiseEmbeddedTransactionNotifications(uint8_t numTransactions, uint8_t numCosignatures) {
			// Arrange:
			mocks::MockTypedNotificationSubscriber<AggregateEmbeddedTransactionNotification> sub;
			auto pRegistry = mocks::CreateDefaultTransactionRegistry();
			auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
			auto wrapper = CreateAggregateTransaction(numTransactions, numCosignatures);

			// Act:
			pPlugin->publish(*wrapper.pTransaction, sub);

			// Assert: the plugin raises an embedded transaction notification for each transaction
			ASSERT_EQ(numTransactions, sub.numMatchingNotifications());
			for (auto i = 0u; i < numTransactions; ++i) {
				auto message = "transaction at " + std::to_string(i);
				const auto& notification = sub.matchingNotifications()[i];

				EXPECT_EQ(wrapper.pTransaction->Signer, notification.Signer) << message;
				EXPECT_EQ(*wrapper.SubTransactions[i], notification.Transaction) << message;
				EXPECT_EQ(numCosignatures, notification.CosignaturesCount);
				EXPECT_EQ(wrapper.pTransaction->CosignaturesPtr(), notification.CosignaturesPtr);
			}
		}
	}

	TEST(TEST_CLASS, EmptyAggregateDoesNotRaiseEmbeddedTransactionNotifications) {
		// Assert:
		AssertCanRaiseEmbeddedTransactionNotifications(0, 0);
	}

	TEST(TEST_CLASS, CanRaiseEmbeddedTransactionNotificationsFromAggregate) {
		// Assert:
		AssertCanRaiseEmbeddedTransactionNotifications(2, 3);
	}

	// endregion

	// region signature

	namespace {
		void AssertCanRaiseSignatureNotifications(uint8_t numTransactions, uint8_t numCosignatures) {
			// Arrange:
			mocks::MockTypedNotificationSubscriber<SignatureNotification> sub;
			auto pRegistry = mocks::CreateDefaultTransactionRegistry();
			auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
			auto wrapper = CreateAggregateTransaction(numTransactions, numCosignatures);

			auto aggregateDataHash = test::GenerateRandomData<Hash256_Size>();

			// Act:
			pPlugin->publish(WeakEntityInfoT<Transaction>(*wrapper.pTransaction, aggregateDataHash), sub);

			// Assert: the plugin only raises signature notifications for explicit cosigners
			//         (the signature notification for the signer is raised as part of normal processing)
			ASSERT_EQ(numCosignatures, sub.numMatchingNotifications());
			for (auto i = 0u; i < numCosignatures; ++i) {
				auto message = "cosigner at " + std::to_string(i);
				const auto& notification = sub.matchingNotifications()[i];

				// - notifications should refer to cosigners
				EXPECT_EQ(wrapper.Cosigners[i], notification.Signer) << message;
				EXPECT_EQ(wrapper.CosignerSignatures[i], notification.Signature) << message;

				// - notifications should refer to same (aggregate) data hash
				EXPECT_EQ(aggregateDataHash.data(), notification.Data.pData) << message;
				EXPECT_EQ(Hash256_Size, notification.Data.Size) << message;
			}
		}
	}

	TEST(TEST_CLASS, EmptyAggregateDoesNotRaiseSignatureNotifications) {
		// Assert:
		AssertCanRaiseSignatureNotifications(0, 0);
	}

	TEST(TEST_CLASS, CanRaiseSignatureNotificationsFromAggregate) {
		// Assert:
		AssertCanRaiseSignatureNotifications(2, 3);
	}

	// endregion

	// region aggregate cosignatures

	TEST(TEST_CLASS, CanRaiseAggregateCosignaturesNotificationsFromEmptyAggregate) {
		// Arrange:
		mocks::MockTypedNotificationSubscriber<AggregateCosignaturesNotification> sub;
		auto pRegistry = mocks::CreateDefaultTransactionRegistry();
		auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
		auto wrapper = CreateAggregateTransaction(0, 0);

		// Act:
		pPlugin->publish(*wrapper.pTransaction, sub);

		// Assert:
		ASSERT_EQ(1u, sub.numMatchingNotifications());
		const auto& notification = sub.matchingNotifications()[0];
		EXPECT_EQ(wrapper.pTransaction->Signer, notification.Signer);
		EXPECT_EQ(0u, notification.TransactionsCount);
		EXPECT_FALSE(!!notification.TransactionsPtr);
		EXPECT_EQ(0u, notification.CosignaturesCount);
		EXPECT_FALSE(!!notification.CosignaturesPtr);
	}

	TEST(TEST_CLASS, CanRaiseAggregateCosignaturesNotificationsFromAggregate) {
		// Arrange:
		mocks::MockTypedNotificationSubscriber<AggregateCosignaturesNotification> sub;
		auto pRegistry = mocks::CreateDefaultTransactionRegistry();
		auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
		auto wrapper = CreateAggregateTransaction(2, 3);

		// Act:
		pPlugin->publish(*wrapper.pTransaction, sub);

		// Assert:
		ASSERT_EQ(1u, sub.numMatchingNotifications());
		const auto& notification = sub.matchingNotifications()[0];
		EXPECT_EQ(wrapper.pTransaction->Signer, notification.Signer);
		EXPECT_EQ(2u, notification.TransactionsCount);
		EXPECT_EQ(wrapper.pTransaction->TransactionsPtr(), notification.TransactionsPtr);
		EXPECT_EQ(3u, notification.CosignaturesCount);
		EXPECT_EQ(wrapper.pTransaction->CosignaturesPtr(), notification.CosignaturesPtr);
	}

	// endregion

	// endregion

	// region dataBuffer

	namespace {
		void AssertCanExtractDataBufferFromAggregate(uint8_t numTransactions, uint8_t numCosignatures) {
			// Arrange:
			auto pRegistry = mocks::CreateDefaultTransactionRegistry();
			auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
			auto wrapper = CreateAggregateTransaction(numTransactions, numCosignatures);

			const auto* pAggregateDataStart = test::AsVoidPointer(&wrapper.pTransaction->Version);
			auto aggregateDataSize = sizeof(AggregateTransaction) - VerifiableEntity::Header_Size;
			aggregateDataSize += numTransactions * sizeof(mocks::EmbeddedMockTransaction);

			// Act:
			auto buffer = pPlugin->dataBuffer(*wrapper.pTransaction);

			// Assert:
			EXPECT_EQ(pAggregateDataStart, buffer.pData);
			EXPECT_EQ(aggregateDataSize, buffer.Size);
		}
	}

	TEST(TEST_CLASS, CanExtractDataBufferFromEmptyAggregate) {
		// Assert:
		AssertCanExtractDataBufferFromAggregate(0, 0);
	}

	TEST(TEST_CLASS, CanExtractDataBufferFromNonEmptyAggregate) {
		// Assert:
		AssertCanExtractDataBufferFromAggregate(2, 3);
	}

	// endregion

	// region merkleSupplementaryBuffers

	namespace {
		void AssertCanExtractMerkleSupplementaryBuffersFromAggregate(uint8_t numTransactions, uint8_t numCosignatures) {
			// Arrange:
			auto pRegistry = mocks::CreateDefaultTransactionRegistry();
			auto pPlugin = CreateAggregateTransactionPlugin(*pRegistry);
			auto wrapper = CreateAggregateTransaction(numTransactions, numCosignatures);

			// Act:
			auto buffers = pPlugin->merkleSupplementaryBuffers(*wrapper.pTransaction);

			// Assert:
			ASSERT_EQ(numCosignatures, buffers.size());

			const auto* pCosignature = wrapper.pTransaction->CosignaturesPtr();
			for (auto i = 0u; i < numCosignatures; ++i) {
				auto message = "buffer " + std::to_string(i);
				EXPECT_EQ(pCosignature->Signer.data(), buffers[i].pData) << message;
				EXPECT_EQ(Key_Size, buffers[i].Size) << message;
				++pCosignature;
			}
		}
	}

	TEST(TEST_CLASS, CanExtractMerkleSupplementaryBuffersFromEmptyAggregate) {
		// Assert:
		AssertCanExtractMerkleSupplementaryBuffersFromAggregate(0, 0);
	}

	TEST(TEST_CLASS, CanExtractMerkleSupplementaryBuffersFromNonEmptyAggregate) {
		// Assert:
		AssertCanExtractMerkleSupplementaryBuffersFromAggregate(2, 3);
	}

	// endregion
}}