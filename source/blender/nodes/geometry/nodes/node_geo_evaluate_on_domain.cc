/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_geometry_util.hh"

#include "NOD_rna_define.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "BKE_geometry_fields.hh"

#include "RNA_enum_types.hh"

#include "NOD_socket_search_link.hh"

namespace blender::nodes::node_geo_evaluate_on_domain_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();
  b.add_default_layout();
  const bNode *node = b.node_or_null();

  if (node != nullptr) {
    const eCustomDataType data_type = eCustomDataType(node->custom2);
    b.add_input(data_type, "Value").supports_field();
    b.add_output(data_type, "Value").field_source_reference_all().align_with_previous();
  }
}

static void node_layout(uiLayout *layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout->prop(ptr, "data_type", UI_ITEM_NONE, "", ICON_NONE);
  layout->prop(ptr, "domain", UI_ITEM_NONE, "", ICON_NONE);
}

static void node_init(bNodeTree * /*tree*/, bNode *node)
{
  node->custom1 = int(AttrDomain::Point);
  node->custom2 = CD_PROP_FLOAT;
}

static void node_gather_link_searches(GatherLinkSearchOpParams &params)
{
  const blender::bke::bNodeType &node_type = params.node_type();
  const std::optional<eCustomDataType> type = bke::socket_type_to_custom_data_type(
      eNodeSocketDatatype(params.other_socket().type));
  if (type && *type != CD_PROP_STRING) {
    params.add_item(IFACE_("Value"), [node_type, type](LinkSearchOpParams &params) {
      bNode &node = params.add_node(node_type);
      node.custom2 = *type;
      params.update_and_connect_available_socket(node, "Value");
    });
  }
}

static void node_geo_exec(GeoNodeExecParams params)
{
  const bNode &node = params.node();
  const AttrDomain domain = AttrDomain(node.custom1);

  GField src_field = params.extract_input<GField>("Value");
  GField dst_field{std::make_shared<bke::EvaluateOnDomainInput>(std::move(src_field), domain)};
  params.set_output<GField>("Value", std::move(dst_field));
}

static void node_rna(StructRNA *srna)
{
  RNA_def_node_enum(srna,
                    "domain",
                    "Domain",
                    "Domain the field is evaluated in",
                    rna_enum_attribute_domain_items,
                    NOD_inline_enum_accessors(custom1),
                    int(AttrDomain::Point));

  RNA_def_node_enum(srna,
                    "data_type",
                    "Data Type",
                    "",
                    rna_enum_attribute_type_items,
                    NOD_inline_enum_accessors(custom2),
                    CD_PROP_FLOAT,
                    enums::attribute_type_type_with_socket_fn);
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeFieldOnDomain", GEO_NODE_EVALUATE_ON_DOMAIN);
  ntype.ui_name = "Evaluate on Domain";
  ntype.ui_description =
      "Retrieve values from a field on a different domain besides the domain from the context";
  ntype.enum_name_legacy = "FIELD_ON_DOMAIN";
  ntype.nclass = NODE_CLASS_CONVERTER;
  ntype.geometry_node_execute = node_geo_exec;
  ntype.draw_buttons = node_layout;
  ntype.initfunc = node_init;
  ntype.declare = node_declare;
  ntype.gather_link_search_ops = node_gather_link_searches;
  blender::bke::node_register_type(ntype);

  node_rna(ntype.rna_ext.srna);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_evaluate_on_domain_cc
