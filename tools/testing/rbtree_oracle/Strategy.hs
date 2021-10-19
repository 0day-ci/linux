{- SPDX-License-Identifier: GPL-2.0-or-later -}
{- Copyright (C) 2021 Mete Polat -}

module Strategy (Result(..), Input, TestCase, random, exhaustive) where


import Control.Applicative
import Control.Monad
import Data.Bifunctor
import Data.List
import Data.Word
import RBT.Kernel (IRBT, Cmd(..))
import RBT.Verified (Tree, Color)
import System.Random (uniform, mkStdGen)
import System.Random.Stateful (Uniform, uniformM, uniformRM)
import qualified RBT.Kernel as Kernel
import qualified RBT.Verified as RBT (empty)
import qualified RBT.Verified as Verified (insert, delete)

type Input = (Cmd,Word64)
type TestCase a = [a]

data Result = Result {
  input :: Input,
  vTree :: IRBT,
  kTreeIO :: Kernel.Handle -> IO IRBT }

instance Uniform Cmd where
  uniformM g = toEnum <$> uniformRM (succ minCmd, maxCmd) g
    where
      minCmd = fromEnum (minBound :: Cmd)
      maxCmd = fromEnum (maxBound :: Cmd)

cmdMap :: Cmd -> (Word64 -> IRBT -> IRBT, Kernel.Handle -> Word64 -> IO IRBT)
cmdMap Insert = (Verified.insert, Kernel.insert)
cmdMap Delete = (Verified.delete, Kernel.delete)
cmdMap Reset = undefined

vCmd :: IRBT -> Input -> IRBT
vCmd t (c,x) = (fst $ cmdMap c) x t

kCmd :: Input -> Kernel.Handle -> IO IRBT
kCmd (c,x) hdl = (snd $ cmdMap c) hdl x

buildInput :: [Word64] -> [Word64] -> [Cmd] -> TestCase Input
buildInput _ _ [] = []
buildInput (i:is) ds (Insert : cs) = (Insert, i) : buildInput is ds cs
buildInput is (d:ds) (Delete : cs) = (Delete, d) : buildInput is ds cs
buildInput _ _ _ = undefined

buildResults :: [TestCase Input] -> [TestCase Result]
buildResults testCases = do
  inputs <- testCases
  let vTrees = tail $ scanl vCmd RBT.empty inputs
  let kTrees = map kCmd inputs
  return $ zipWith3 Result inputs vTrees kTrees

random :: Word64 -> Int -> [TestCase Result]
random runs seed = do
  let rndCmds = randoms seed
  let rndXs = randoms seed
  let inputs = genericTake runs (buildInput rndXs rndXs rndCmds)
  buildResults [inputs]
  where
    randoms :: Uniform a => Int -> [a]
    randoms = unfoldr (Just . uniform) . mkStdGen

exhaustive :: Word64 -> [TestCase Result]
exhaustive n = do
  let distributions = [genericReplicate i Insert ++ genericReplicate (n-i) Delete | i <- [n,n-1..]]
  let inputRuns = concatMap (permutations . buildInput [1..n] [1..n]) distributions
  buildResults inputRuns
