(* SPDX-License-Identifier: GPL-2.0-or-later *)
(* Copyright (C) 2021 Mete Polat *)

theory RBT_export
imports
  "HOL-Data_Structures.RBT_Set"
  "HOL-Data_Structures.Tree2"
  "HOL-Library.Code_Target_Numeral"
begin

(*
Map all the different orderings to Haskell's linorder class.
This is fine because the RBT interface only uses linorder.
*)

code_printing
  type_class ord \<rightharpoonup> (Haskell) "Prelude.Ord"
| constant Orderings.less \<rightharpoonup> (Haskell) infixl 4 "<"
| constant Orderings.less_eq \<rightharpoonup> (Haskell) infixl 4 "<="
| type_class order \<rightharpoonup> (Haskell) "Prelude.Ord"
| type_class preorder \<rightharpoonup> (Haskell) "Prelude.Ord"
| type_class linorder \<rightharpoonup> (Haskell) "Prelude.Ord"

definition rootBlack :: "'a rbt \<Rightarrow> bool" where
"rootBlack t \<equiv> color t = Black"

definition rbt :: "'a rbt \<Rightarrow> bool" where
"rbt t \<equiv> invc t \<and> invh t \<and> rootBlack t"

lemma "RBT_Set.rbt t \<equiv> rbt t"
  by (simp add: RBT_Set.rbt_def rbt_def rootBlack_def)

export_code
  rootBlack rbt
  RBT_Set.invc RBT_Set.invh
  RBT_Set.empty RBT_Set.insert RBT_Set.delete
  Tree2.inorder
  equal_tree_inst.equal_tree
in Haskell module_name RBT.Verified (string_classes)

end
