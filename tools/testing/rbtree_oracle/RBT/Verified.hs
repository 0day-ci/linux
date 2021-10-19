{- SPDX-License-Identifier: BSD-3-Clause -}
{- Copyright (c) 1986-2021,
   University of Cambridge,
   Technische Universitaet Muenchen,
   and contributors.

   All rights reserved.
-}

{-# LANGUAGE EmptyDataDecls, RankNTypes, ScopedTypeVariables #-}

module
  RBT.Verified(Color, Nat, Tree, equal_tree, invc, invh, empty, inorder, delete,
                insert, rootBlack, rbt)
  where {

import Prelude ((==), (/=), (<), (<=), (>=), (>), (+), (-), (*), (/), (**),
  (>>=), (>>), (=<<), (&&), (||), (^), (^^), (.), ($), ($!), (++), (!!), Eq,
  error, id, return, not, fst, snd, map, filter, concat, concatMap, reverse,
  zip, null, takeWhile, dropWhile, all, any, Integer, negate, abs, divMod,
  String, Bool(True, False), Maybe(Nothing, Just));
import qualified Prelude;

data Color = Red | Black deriving (Prelude.Read, Prelude.Show);

equal_color :: Color -> Color -> Bool;
equal_color Red Black = False;
equal_color Black Red = False;
equal_color Black Black = True;
equal_color Red Red = True;

instance Eq Color where {
  a == b = equal_color a b;
};

newtype Nat = Nat Integer deriving (Prelude.Read, Prelude.Show);

data Num = One | Bit0 Num | Bit1 Num deriving (Prelude.Read, Prelude.Show);

data Tree a = Leaf | Node (Tree a) a (Tree a)
  deriving (Prelude.Read, Prelude.Show);

data Cmp_val = LT | EQ | GT deriving (Prelude.Read, Prelude.Show);

cmp :: forall a. (Eq a, Prelude.Ord a) => a -> a -> Cmp_val;
cmp x y = (if x < y then LT else (if x == y then EQ else GT));

paint :: forall a. Color -> Tree (a, Color) -> Tree (a, Color);
paint c Leaf = Leaf;
paint c (Node l (a, uu) r) = Node l (a, c) r;

baliR :: forall a. Tree (a, Color) -> a -> Tree (a, Color) -> Tree (a, Color);
baliR t1 a (Node t2 (b, Red) (Node t3 (c, Red) t4)) =
  Node (Node t1 (a, Black) t2) (b, Red) (Node t3 (c, Black) t4);
baliR t1 a (Node (Node t2 (b, Red) t3) (c, Red) Leaf) =
  Node (Node t1 (a, Black) t2) (b, Red) (Node t3 (c, Black) Leaf);
baliR t1 a (Node (Node t2 (b, Red) t3) (c, Red) (Node v (vc, Black) vb)) =
  Node (Node t1 (a, Black) t2) (b, Red)
    (Node t3 (c, Black) (Node v (vc, Black) vb));
baliR t1 a Leaf = Node t1 (a, Black) Leaf;
baliR t1 a (Node v (vc, Black) vb) = Node t1 (a, Black) (Node v (vc, Black) vb);
baliR t1 a (Node Leaf va Leaf) = Node t1 (a, Black) (Node Leaf va Leaf);
baliR t1 a (Node (Node vb (ve, Black) vd) va Leaf) =
  Node t1 (a, Black) (Node (Node vb (ve, Black) vd) va Leaf);
baliR t1 a (Node Leaf va (Node vc (vf, Black) ve)) =
  Node t1 (a, Black) (Node Leaf va (Node vc (vf, Black) ve));
baliR t1 a (Node (Node vb (vh, Black) vg) va (Node vc (vf, Black) ve)) =
  Node t1 (a, Black)
    (Node (Node vb (vh, Black) vg) va (Node vc (vf, Black) ve));

baldL :: forall a. Tree (a, Color) -> a -> Tree (a, Color) -> Tree (a, Color);
baldL (Node t1 (a, Red) t2) b t3 = Node (Node t1 (a, Black) t2) (b, Red) t3;
baldL Leaf a (Node t2 (b, Black) t3) = baliR Leaf a (Node t2 (b, Red) t3);
baldL (Node v (vc, Black) vb) a (Node t2 (b, Black) t3) =
  baliR (Node v (vc, Black) vb) a (Node t2 (b, Red) t3);
baldL Leaf a (Node (Node t2 (b, Black) t3) (c, Red) t4) =
  Node (Node Leaf (a, Black) t2) (b, Red) (baliR t3 c (paint Red t4));
baldL (Node v (vc, Black) vb) a (Node (Node t2 (b, Black) t3) (c, Red) t4) =
  Node (Node (Node v (vc, Black) vb) (a, Black) t2) (b, Red)
    (baliR t3 c (paint Red t4));
baldL Leaf a Leaf = Node Leaf (a, Red) Leaf;
baldL Leaf a (Node Leaf (vc, Red) vb) =
  Node Leaf (a, Red) (Node Leaf (vc, Red) vb);
baldL Leaf a (Node (Node va (vf, Red) ve) (vc, Red) vb) =
  Node Leaf (a, Red) (Node (Node va (vf, Red) ve) (vc, Red) vb);
baldL (Node v (vc, Black) vb) a Leaf =
  Node (Node v (vc, Black) vb) (a, Red) Leaf;
baldL (Node v (vc, Black) vb) a (Node Leaf (vf, Red) ve) =
  Node (Node v (vc, Black) vb) (a, Red) (Node Leaf (vf, Red) ve);
baldL (Node v (vc, Black) vb) a (Node (Node vd (vi, Red) vh) (vf, Red) ve) =
  Node (Node v (vc, Black) vb) (a, Red)
    (Node (Node vd (vi, Red) vh) (vf, Red) ve);

join :: forall a. Tree (a, Color) -> Tree (a, Color) -> Tree (a, Color);
join Leaf t = t;
join (Node v va vb) Leaf = Node v va vb;
join (Node t1 (a, Red) t2) (Node t3 (c, Red) t4) =
  (case join t2 t3 of {
    Leaf -> Node t1 (a, Red) (Node Leaf (c, Red) t4);
    Node u2 (b, Red) u3 ->
      Node (Node t1 (a, Red) u2) (b, Red) (Node u3 (c, Red) t4);
    Node u2 (b, Black) u3 ->
      Node t1 (a, Red) (Node (Node u2 (b, Black) u3) (c, Red) t4);
  });
join (Node t1 (a, Black) t2) (Node t3 (c, Black) t4) =
  (case join t2 t3 of {
    Leaf -> baldL t1 a (Node Leaf (c, Black) t4);
    Node u2 (b, Red) u3 ->
      Node (Node t1 (a, Black) u2) (b, Red) (Node u3 (c, Black) t4);
    Node u2 (b, Black) u3 ->
      baldL t1 a (Node (Node u2 (b, Black) u3) (c, Black) t4);
  });
join (Node v (vc, Black) vb) (Node t2 (a, Red) t3) =
  Node (join (Node v (vc, Black) vb) t2) (a, Red) t3;
join (Node t1 (a, Red) t2) (Node v (vc, Black) vb) =
  Node t1 (a, Red) (join t2 (Node v (vc, Black) vb));

baliL :: forall a. Tree (a, Color) -> a -> Tree (a, Color) -> Tree (a, Color);
baliL (Node (Node t1 (a, Red) t2) (b, Red) t3) c t4 =
  Node (Node t1 (a, Black) t2) (b, Red) (Node t3 (c, Black) t4);
baliL (Node Leaf (a, Red) (Node t2 (b, Red) t3)) c t4 =
  Node (Node Leaf (a, Black) t2) (b, Red) (Node t3 (c, Black) t4);
baliL (Node (Node v (vc, Black) vb) (a, Red) (Node t2 (b, Red) t3)) c t4 =
  Node (Node (Node v (vc, Black) vb) (a, Black) t2) (b, Red)
    (Node t3 (c, Black) t4);
baliL Leaf a t2 = Node Leaf (a, Black) t2;
baliL (Node Leaf (v, Black) vb) a t2 =
  Node (Node Leaf (v, Black) vb) (a, Black) t2;
baliL (Node Leaf va Leaf) a t2 = Node (Node Leaf va Leaf) (a, Black) t2;
baliL (Node Leaf va (Node v (ve, Black) vd)) a t2 =
  Node (Node Leaf va (Node v (ve, Black) vd)) (a, Black) t2;
baliL (Node (Node vc (vf, Black) ve) (v, Black) vb) a t2 =
  Node (Node (Node vc (vf, Black) ve) (v, Black) vb) (a, Black) t2;
baliL (Node (Node vc (vf, Black) ve) va Leaf) a t2 =
  Node (Node (Node vc (vf, Black) ve) va Leaf) (a, Black) t2;
baliL (Node (Node vc (vf, Black) ve) va (Node v (vh, Black) vg)) a t2 =
  Node (Node (Node vc (vf, Black) ve) va (Node v (vh, Black) vg)) (a, Black) t2;
baliL (Node v (vc, Black) vb) a t2 = Node (Node v (vc, Black) vb) (a, Black) t2;

baldR :: forall a. Tree (a, Color) -> a -> Tree (a, Color) -> Tree (a, Color);
baldR t1 a (Node t2 (b, Red) t3) = Node t1 (a, Red) (Node t2 (b, Black) t3);
baldR (Node t1 (a, Black) t2) b Leaf = baliL (Node t1 (a, Red) t2) b Leaf;
baldR (Node t1 (a, Black) t2) b (Node v (vc, Black) vb) =
  baliL (Node t1 (a, Red) t2) b (Node v (vc, Black) vb);
baldR (Node t1 (a, Red) (Node t2 (b, Black) t3)) c Leaf =
  Node (baliL (paint Red t1) a t2) (b, Red) (Node t3 (c, Black) Leaf);
baldR (Node t1 (a, Red) (Node t2 (b, Black) t3)) c (Node v (vc, Black) vb) =
  Node (baliL (paint Red t1) a t2) (b, Red)
    (Node t3 (c, Black) (Node v (vc, Black) vb));
baldR Leaf a Leaf = Node Leaf (a, Red) Leaf;
baldR (Node v (vc, Red) Leaf) a Leaf =
  Node (Node v (vc, Red) Leaf) (a, Red) Leaf;
baldR (Node v (vc, Red) (Node va (vf, Red) ve)) a Leaf =
  Node (Node v (vc, Red) (Node va (vf, Red) ve)) (a, Red) Leaf;
baldR Leaf a (Node v (vc, Black) vb) =
  Node Leaf (a, Red) (Node v (vc, Black) vb);
baldR (Node va (vf, Red) Leaf) a (Node v (vc, Black) vb) =
  Node (Node va (vf, Red) Leaf) (a, Red) (Node v (vc, Black) vb);
baldR (Node va (vf, Red) (Node vd (vi, Red) vh)) a (Node v (vc, Black) vb) =
  Node (Node va (vf, Red) (Node vd (vi, Red) vh)) (a, Red)
    (Node v (vc, Black) vb);

equal_tree :: forall a. (Eq a) => Tree a -> Tree a -> Bool;
equal_tree Leaf (Node x21 x22 x23) = False;
equal_tree (Node x21 x22 x23) Leaf = False;
equal_tree (Node x21 x22 x23) (Node y21 y22 y23) =
  equal_tree x21 y21 && x22 == y22 && equal_tree x23 y23;
equal_tree Leaf Leaf = True;

color :: forall a. Tree (a, Color) -> Color;
color Leaf = Black;
color (Node uu (uv, c) uw) = c;

del ::
  forall a. (Eq a, Prelude.Ord a) => a -> Tree (a, Color) -> Tree (a, Color);
del x Leaf = Leaf;
del x (Node l (a, uu) r) =
  (case cmp x a of {
    LT -> (if not (equal_tree l Leaf) && equal_color (color l) Black
            then baldL (del x l) a r else Node (del x l) (a, Red) r);
    EQ -> join l r;
    GT -> (if not (equal_tree r Leaf) && equal_color (color r) Black
            then baldR l a (del x r) else Node l (a, Red) (del x r));
  });

ins ::
  forall a. (Eq a, Prelude.Ord a) => a -> Tree (a, Color) -> Tree (a, Color);
ins x Leaf = Node Leaf (x, Red) Leaf;
ins x (Node l (a, Black) r) = (case cmp x a of {
                                LT -> baliL (ins x l) a r;
                                EQ -> Node l (a, Black) r;
                                GT -> baliR l a (ins x r);
                              });
ins x (Node l (a, Red) r) = (case cmp x a of {
                              LT -> Node (ins x l) (a, Red) r;
                              EQ -> Node l (a, Red) r;
                              GT -> Node l (a, Red) (ins x r);
                            });

invc :: forall a. Tree (a, Color) -> Bool;
invc Leaf = True;
invc (Node l (a, c) r) =
  (if equal_color c Red
    then equal_color (color l) Black && equal_color (color r) Black
    else True) &&
    invc l && invc r;

integer_of_nat :: Nat -> Integer;
integer_of_nat (Nat x) = x;

equal_nat :: Nat -> Nat -> Bool;
equal_nat m n = integer_of_nat m == integer_of_nat n;

zero_nat :: Nat;
zero_nat = Nat (0 :: Integer);

plus_nat :: Nat -> Nat -> Nat;
plus_nat m n = Nat (integer_of_nat m + integer_of_nat n);

one_nat :: Nat;
one_nat = Nat (1 :: Integer);

bheight :: forall a. Tree (a, Color) -> Nat;
bheight Leaf = zero_nat;
bheight (Node l (x, c) r) =
  (if equal_color c Black then plus_nat (bheight l) one_nat else bheight l);

invh :: forall a. Tree (a, Color) -> Bool;
invh Leaf = True;
invh (Node l (x, c) r) = equal_nat (bheight l) (bheight r) && invh l && invh r;

empty :: forall a. Tree (a, Color);
empty = Leaf;

inorder :: forall a b. Tree (a, b) -> [a];
inorder Leaf = [];
inorder (Node l (a, uu) r) = inorder l ++ a : inorder r;

delete ::
  forall a. (Eq a, Prelude.Ord a) => a -> Tree (a, Color) -> Tree (a, Color);
delete x t = paint Black (del x t);

insert ::
  forall a. (Eq a, Prelude.Ord a) => a -> Tree (a, Color) -> Tree (a, Color);
insert x t = paint Black (ins x t);

rootBlack :: forall a. Tree (a, Color) -> Bool;
rootBlack t = equal_color (color t) Black;

rbt :: forall a. Tree (a, Color) -> Bool;
rbt t = invc t && invh t && rootBlack t;

}
