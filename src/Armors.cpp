#include "Armors.h"

#include <regex>
#include <any>

#include "Parsers.h"
#include "Utils.h"

namespace Armors {
	enum class FilterType {
		kFormID
	};

	std::string_view FilterTypeToString(FilterType a_value) {
		switch (a_value) {
		case FilterType::kFormID: return "FilterByFormID";
		default: return std::string_view{};
		}
	}

	enum class ElementType {
		kBipedObjectSlots,
		kFullName,
		kObjectEffect
	};

	std::string_view ElementTypeToString(ElementType a_value) {
		switch (a_value) {
		case ElementType::kBipedObjectSlots: return "BipedObjectSlots";
		case ElementType::kFullName: return "FullName";
		case ElementType::kObjectEffect: return "ObjectEffect";
		default: return std::string_view{};
		}
	}

	struct ConfigData {
		FilterType Filter;
		std::string FilterForm;
		ElementType Element;
		std::optional<std::any> AssignValue;
	};

	struct PatchData {
		std::optional<std::uint32_t> BipedObjectSlots;
		std::optional<std::string> FullName;
		std::optional<RE::EnchantmentItem*> ObjectEffect;
	};

	std::vector<Parsers::Statement<ConfigData>> g_configVec;
	std::unordered_map<RE::TESObjectARMO*, PatchData> g_patchMap;

	class ArmorParser : public Parsers::Parser<ConfigData> {
	public:
		ArmorParser(std::string_view a_configPath) : Parsers::Parser<ConfigData>(a_configPath) {}

	protected:
		std::optional<Parsers::Statement<ConfigData>> ParseExpressionStatement() override {
			if (reader.EndOfFile() || reader.Peek().empty()) {
				return std::nullopt;
			}

			ConfigData configData{};

			if (!ParseFilter(configData)) {
				return std::nullopt;
			}

			auto token = reader.GetToken();
			if (token != ".") {
				logger::warn("Line {}, Col {}: Syntax error. Expected '.'.", reader.GetLastLine(), reader.GetLastLineIndex());
				return std::nullopt;
			}

			if (!ParseElement(configData)) {
				return std::nullopt;
			}

			if (!ParseAssignment(configData)) {
				return std::nullopt;
			}

			token = reader.GetToken();
			if (token != ";") {
				logger::warn("Line {}, Col {}: Syntax error. Expected ';'.", reader.GetLastLine(), reader.GetLastLineIndex());
				return std::nullopt;
			}

			return Parsers::Statement<ConfigData>::CreateExpressionStatement(configData);
		}

		void PrintExpressionStatement(const ConfigData& a_configData, int a_indent) override {
			std::string indent = std::string(a_indent * 4, ' ');

			switch (a_configData.Element) {
			case ElementType::kBipedObjectSlots:
				logger::info("{}{}({}).{} = {};", indent, FilterTypeToString(a_configData.Filter), a_configData.FilterForm,
					ElementTypeToString(a_configData.Element), GetBipedSlots(std::any_cast<std::uint32_t>(a_configData.AssignValue.value())));
				break;

			case ElementType::kFullName:
				logger::info("{}{}({}).{} = \"{}\";", indent, FilterTypeToString(a_configData.Filter), a_configData.FilterForm,
					ElementTypeToString(a_configData.Element), std::any_cast<std::string>(a_configData.AssignValue.value()));
				break;

			case ElementType::kObjectEffect:
				logger::info("{}{}({}).{} = {};", indent, FilterTypeToString(a_configData.Filter), a_configData.FilterForm,
					ElementTypeToString(a_configData.Element), std::any_cast<std::string>(a_configData.AssignValue.value()));
				break;
			}
		}

		bool ParseFilter(ConfigData& a_config) {
			auto token = reader.GetToken();
			if (token == "FilterByFormID") {
				a_config.Filter = FilterType::kFormID;
			}
			else {
				logger::warn("Line {}, Col {}: Invalid FilterName '{}'.", reader.GetLastLine(), reader.GetLastLineIndex(), token);
				return false;
			}

			token = reader.GetToken();
			if (token != "(") {
				logger::warn("Line {}, Col {}: Syntax error. Expected '('.", reader.GetLastLine(), reader.GetLastLineIndex());
				return false;
			}

			auto filterForm = ParseForm();
			if (!filterForm.has_value()) {
				return false;
			}

			a_config.FilterForm = filterForm.value();

			token = reader.GetToken();
			if (token != ")") {
				logger::warn("Line {}, Col {}: Syntax error. Expected ')'.", reader.GetLastLine(), reader.GetLastLineIndex());
				return false;
			}

			return true;
		}

		bool ParseElement(ConfigData& a_config) {
			auto token = reader.GetToken();
			if (token == "BipedObjectSlots") {
				a_config.Element = ElementType::kBipedObjectSlots;
			}
			else if (token == "FullName") {
				a_config.Element = ElementType::kFullName;
			}
			else if (token == "ObjectEffect") {
				a_config.Element = ElementType::kObjectEffect;
			}
			else {
				logger::warn("Line {}, Col {}: Invalid ElementName '{}'.", reader.GetLastLine(), reader.GetLastLineIndex(), token);
				return false;
			}

			return true;
		}

		bool ParseAssignment(ConfigData& a_config) {
			auto token = reader.GetToken();
			if (token != "=") {
				logger::warn("Line {}, Col {}: Syntax error. Expected '='.", reader.GetLastLine(), reader.GetLastLineIndex());
				return false;
			}

			if (a_config.Element == ElementType::kBipedObjectSlots) {
				std::uint32_t bipedObjectSlotsValue = 0;

				auto bipedSlot = ParseBipedSlot();
				if (!bipedSlot.has_value()) {
					return false;
				}

				if (bipedSlot.value() != 0) {
					bipedObjectSlotsValue |= 1 << (bipedSlot.value() - 30);
				}

				while (true) {
					token = reader.Peek();
					if (token == ";") {
						break;
					}

					token = reader.GetToken();
					if (token != "|") {
						logger::warn("Line {}, Col {}: Syntax error. Expected '|' or ';'.", reader.GetLastLine(), reader.GetLastLineIndex());
						return false;
					}

					bipedSlot = ParseBipedSlot();
					if (!bipedSlot.has_value()) {
						return false;
					}

					if (bipedSlot.value() != 0) {
						bipedObjectSlotsValue |= 1 << (bipedSlot.value() - 30);
					}
				}

				a_config.AssignValue = std::any(bipedObjectSlotsValue);
			}
			else if (a_config.Element == ElementType::kFullName) {
				token = reader.GetToken();
				if (!token.starts_with('\"')) {
					logger::warn("Line {}, Col {}: FullName must be a string.", reader.GetLastLine(), reader.GetLastLineIndex());
					return false;
				}
				else if (!token.ends_with('\"')) {
					logger::warn("Line {}, Col {}: String must end with '\"'.", reader.GetLastLine(), reader.GetLastLineIndex());
					return false;
				}

				std::string value = std::string(token.substr(1, token.length() - 2));
				a_config.AssignValue = std::any(value);
			}
			else if (a_config.Element == ElementType::kObjectEffect) {
				token = reader.Peek();
				if (token == "null") {
					a_config.AssignValue = std::any(std::string(reader.GetToken()));
				} else {
					auto effectForm = ParseForm();
					if (!effectForm.has_value()) {
						return false;
					}

					a_config.AssignValue = std::any(std::string(effectForm.value()));
				}
			}
			else {
				logger::warn("Line {}, Col {}: Invalid Assignment to {}.", reader.GetLastLine(), reader.GetLastLineIndex(), ElementTypeToString(a_config.Element));
				return false;
			}

			return true;
		}
	};

	void ReadConfig(std::string_view a_path) {
		ArmorParser parser(a_path);
		auto parsedStatements = parser.Parse();
		g_configVec.insert(g_configVec.end(), parsedStatements.begin(), parsedStatements.end());
	}

	void ReadConfigs() {
		const std::filesystem::path configDir{ "Data\\" + std::string(Version::PROJECT) + "\\Armor" };
		if (!std::filesystem::exists(configDir)) {
			return;
		}

		const std::regex filter(".*\\.cfg", std::regex_constants::icase);
		const std::filesystem::directory_iterator dir_iter(configDir);
		for (auto& iter : dir_iter) {
			if (!std::filesystem::is_regular_file(iter.status())) {
				continue;
			}

			if (!std::regex_match(iter.path().filename().string(), filter)) {
				continue;
			}

			std::string path = iter.path().string();
			logger::info("=========== Reading Armor config file: {} ===========", path);
			ReadConfig(path);
			logger::info("");
		}
	}

	void Prepare(const ConfigData& a_configData) {
		if (a_configData.Filter == FilterType::kFormID) {
			RE::TESForm* filterForm = Utils::GetFormFromString(a_configData.FilterForm);
			if (!filterForm) {
				logger::warn("Invalid FilterForm: '{}'.", a_configData.FilterForm);
				return;
			}

			RE::TESObjectARMO* armo = filterForm->As<RE::TESObjectARMO>();
			if (!armo) {
				logger::warn("'{}' is not a Armor.", a_configData.FilterForm);
				return;
			}

			if (a_configData.Element == ElementType::kBipedObjectSlots) {
				if (a_configData.AssignValue.has_value()) {
					g_patchMap[armo].BipedObjectSlots = std::any_cast<std::uint32_t>(a_configData.AssignValue.value());
				}
			}
			else if (a_configData.Element == ElementType::kFullName) {
				if (a_configData.AssignValue.has_value()) {
					g_patchMap[armo].FullName = std::any_cast<std::string>(a_configData.AssignValue.value());
				}
			}
			else if (a_configData.Element == ElementType::kObjectEffect) {
				if (!a_configData.AssignValue.has_value()) {
					return;
				}

				std::string effectFormStr = std::any_cast<std::string>(a_configData.AssignValue.value());

				if (effectFormStr == "null") {
					g_patchMap[armo].ObjectEffect = nullptr;
				} else {
					RE::TESForm* effectForm = Utils::GetFormFromString(effectFormStr);
					if (!effectForm) {
						logger::warn("Invalid Form: '{}'.", effectFormStr);
						return;
					}

					RE::EnchantmentItem* objectEffect = effectForm->As<RE::EnchantmentItem>();
					if (!objectEffect) {
						logger::warn("'{}' is not an Object Effect.", effectFormStr);
						return;
					}

					g_patchMap[armo].ObjectEffect = objectEffect;
				}
			}
		}
	}

	void Prepare(const std::vector<Parsers::Statement<ConfigData>>& a_configVec) {
		for (const auto& configData : a_configVec) {
			if (configData.Type == Parsers::StatementType::kExpression) {
				Prepare(configData.ExpressionStatement.value());
			}
			else if (configData.Type == Parsers::StatementType::kConditional) {
				Prepare(configData.ConditionalStatement->Evaluates());
			}
		}
	}

	void Patch() {
		logger::info("======================== Start preparing patch for Armor ========================");

		Prepare(g_configVec);

		logger::info("======================== Finished preparing patch for Armor ========================");
		logger::info("");

		logger::info("======================== Start patching for Armor ========================");

		for (const auto& patchData : g_patchMap) {
			if (patchData.second.BipedObjectSlots.has_value()) {
				patchData.first->bipedModelData.bipedObjectSlots = patchData.second.BipedObjectSlots.value();
			}
			if (patchData.second.FullName.has_value()) {
				patchData.first->fullName = patchData.second.FullName.value();
			}
			if (patchData.second.ObjectEffect.has_value()) {
				patchData.first->formEnchanting = patchData.second.ObjectEffect.value();
			}
		}

		logger::info("======================== Finished patching for Armor ========================");
		logger::info("");

		g_configVec.clear();
		g_patchMap.clear();
	}
}
