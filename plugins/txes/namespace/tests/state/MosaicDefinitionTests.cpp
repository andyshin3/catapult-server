#include "src/state/MosaicDefinition.h"
#include "src/model/MosaicProperties.h"
#include "tests/test/MosaicTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace state {

	namespace {
		constexpr Height Default_Height(345);

		void AssertDefaultRequiredProperties(const model::MosaicProperties& properties) {
			EXPECT_FALSE(properties.is(model::MosaicFlags::Supply_Mutable));
			EXPECT_FALSE(properties.is(model::MosaicFlags::Transferable));
			EXPECT_FALSE(properties.is(model::MosaicFlags::Levy_Mutable));
			EXPECT_EQ(0u, properties.divisibility());
		}

		void AssertCustomOptionalProperties(const model::MosaicProperties& expectedProperties, const model::MosaicProperties& properties) {
			EXPECT_EQ(expectedProperties.duration(), properties.duration());
		}

		MosaicDefinition CreateMosaicDefinition(uint64_t duration) {
			auto owner = test::GenerateRandomData<Key_Size>();
			return MosaicDefinition(Default_Height, owner, test::CreateMosaicPropertiesWithDuration(ArtifactDuration(duration)));
		}
	}

	// region ctor

	TEST(MosaicDefinitionTests, CanCreateMosaicDefinition_DefaultProperties) {
		// Arrange:
		auto owner = test::GenerateRandomData<Key_Size>();
		auto properties = model::MosaicProperties::FromValues({});

		// Act:
		MosaicDefinition definition(Height(877), owner, properties);

		// Assert:
		EXPECT_EQ(Height(877), definition.height());
		EXPECT_EQ(owner, definition.owner());
		AssertDefaultRequiredProperties(definition.properties());
		AssertCustomOptionalProperties(properties, definition.properties());
	}

	TEST(MosaicDefinitionTests, CanCreateMosaicDefinition_CustomProperties) {
		// Arrange:
		auto owner = test::GenerateRandomData<Key_Size>();
		auto properties = test::CreateMosaicPropertiesWithDuration(ArtifactDuration(3));

		// Act:
		MosaicDefinition definition(Height(877), owner, properties);

		// Assert:
		EXPECT_EQ(Height(877), definition.height());
		EXPECT_EQ(owner, definition.owner());
		AssertDefaultRequiredProperties(definition.properties());
		AssertCustomOptionalProperties(properties, definition.properties());
	}

	// endregion

	// region isEternal

	TEST(MosaicDefinitionTests, IsEternalReturnsTrueIfMosaicDefinitionHasEternalDuration) {
		// Arrange:
		auto definition = CreateMosaicDefinition(Eternal_Artifact_Duration.unwrap());

		// Assert:
		EXPECT_TRUE(definition.isEternal());
	}

	TEST(MosaicDefinitionTests, IsEternalReturnsFalseIfMosaicDefinitionDoesNotHaveEternalDuration) {
		// Arrange:
		for (auto duration : { 1u, 2u, 1000u, 10000u, 1'000'000'000u }) {
			auto definition = CreateMosaicDefinition(duration);

			// Assert:
			EXPECT_FALSE(definition.isEternal()) << "duration " << duration;
		}
	}

	// endregion

	// region isActive

	namespace {
		void AssertActiveOrNot(
				ArtifactDuration::ValueType duration,
				const std::vector<Height::ValueType>& heights,
				bool expectedResult) {
			// Arrange: creation height is 345
			auto definition = CreateMosaicDefinition(duration);

			// Assert:
			for (auto height : heights)
				EXPECT_EQ(expectedResult, definition.isActive(Height(height))) << "at height " << height;
		}
	}

	TEST(MosaicDefinitionTests, IsActiveReturnsTrueIfMosaicDefinitionIsActive) {
		// Assert:
		auto duration = 57u;
		auto height = Default_Height.unwrap();
		AssertActiveOrNot(duration, { height, height + 1, height + 22, height + duration - 2, height + duration - 1 }, true);
	}

	TEST(MosaicDefinitionTests, IsActiveReturnsTrueIfMosaicDefinitionIsEternal) {
		// Assert:
		auto duration = Eternal_Artifact_Duration.unwrap();
		auto height = Default_Height.unwrap();
		AssertActiveOrNot(duration, { height - 1, height, height + 1, 500u, 5000u, std::numeric_limits<Height::ValueType>::max() }, true);
	}

	TEST(MosaicDefinitionTests, IsActiveReturnsFalseIfMosaicDefinitionIsNotActive) {
		// Assert:
		auto duration = 57u;
		auto height = Default_Height.unwrap();
		AssertActiveOrNot(duration, { 1u, height - 2, height - 1, height + duration, height + duration + 1, height + 10'000 }, false);
	}

	// endregion

	// region isExpired

	namespace {
		void AssertExpiredOrNot(
				ArtifactDuration::ValueType duration,
				const std::vector<Height::ValueType>& heights,
				bool expectedResult) {
			// Arrange: creation height is 345
			auto definition = CreateMosaicDefinition(duration);

			// Assert:
			for (auto height : heights)
				EXPECT_EQ(expectedResult, definition.isExpired(Height(height))) << "at height " << height;
		}
	}

	TEST(MosaicDefinitionTests, IsExpiredReturnsTrueIfMosaicDefinitionIsExpired) {
		// Assert:
		auto duration = 57u;
		auto height = Default_Height.unwrap();
		AssertExpiredOrNot(duration, { height + duration, height + duration + 1, height + 10'000 }, true);
	}

	TEST(MosaicDefinitionTests, IsExpiredReturnsFalseIfMosaicDefinitionIsEternal) {
		// Assert:
		auto duration = Eternal_Artifact_Duration.unwrap();
		auto height = Default_Height.unwrap();
		AssertExpiredOrNot(duration, { 1, height - 1, height, height + 1, 5000u, std::numeric_limits<Height::ValueType>::max() }, false);
	}

	TEST(MosaicDefinitionTests, IsExpiredReturnsFalseIfMosaicDefinitionIsNotExpired) {
		// Assert:
		auto duration = 57u;
		auto height = Default_Height.unwrap();
		AssertExpiredOrNot(duration, { 1, height - 1, height, height + 1, height + duration - 2, height + duration - 1 }, false);
	}

	// endregion
}}