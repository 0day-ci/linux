{- SPDX-License-Identifier: GPL-2.0-or-later -}
{- Copyright (C) 2021 Mete Polat -}

module Main where

import Control.Monad
import Control.Monad.Reader
import Data.Monoid
import Data.Word
import Options.Applicative
import RBT.Kernel (IRBT, Cmd(..))
import RBT.Verified
import Strategy
import qualified RBT.Kernel as Kernel
import qualified RBT.Verified as RBT (empty, equal_tree)
import qualified RBT.Verified as Verified (insert, delete)

data Options = Options {
  verbose :: Bool,
  strategy :: Strategy }

type OptionsT m a = ReaderT Options m a

data RandomOptions = RandomOptions {
  n :: Word64,
  seed :: Int }

data ExhaustiveOptions = ExhaustiveOptions { n :: Word64 }

data Strategy = Random RandomOptions | Exhaustive ExhaustiveOptions

restartHeader = "------Restart------\n"


main :: IO ()
main = do
  options@Options{..} <- execParser options

  let rss = case strategy of
        Random (RandomOptions n seed) -> Strategy.random n seed
        Exhaustive (ExhaustiveOptions n) -> Strategy.exhaustive n

  unless (null rss) $ do
    hdl <- Kernel.init

    forM_ rss $ \rs -> do
      Kernel.reset hdl
      when verbose $ putStrLn restartHeader
      runReaderT (checkResults rs RBT.empty hdl) options

    Kernel.cleanup hdl


invariantCompare :: IRBT -> IRBT -> Either [String] ()
invariantCompare vTree kTree = unless (rbt kTree && inorder kTree == inorder vTree) $
  Left $ map fst $ filter (not . snd) [
      ("color"     ,  invc kTree) ,
      ("height"    ,  invh kTree) ,
      ("root_black",  rootBlack kTree) ,
      ("inorder"   ,  inorder vTree == inorder kTree) ]


printTrees :: Input -> IRBT -> IRBT -> IRBT -> [String] -> IO ()
printTrees (cmd,key) vTree kTree kTreePrev invs = do
  putStrLn $ unwords $ if null invs
  then [show cmd, show key]
  else ["After", show cmd, show key, "following invariants failed:"] ++ invs
  putStrLn $ "Kernel tree before:  " ++ show kTreePrev
  putStrLn $ "Kernel tree after:   " ++ show kTree
  putStrLn $ "Verified tree after: " ++ show vTree
  putStrLn ""


checkResults :: [Result] -> IRBT -> Kernel.Handle -> OptionsT IO ()
checkResults [] _ _  = liftIO $ return ()
checkResults (Result{..}:rs) kTreePrev hdl = do
  kTree <- liftIO $ kTreeIO hdl
  case invariantCompare vTree kTree of
    Left invs -> liftIO $
      printTrees input vTree kTree kTreePrev invs
    Right _ -> do
      v <- asks verbose
      when v $ liftIO $ printTrees input vTree kTree kTreePrev []
      checkResults rs kTree hdl


{- HLINT ignore options "Monoid law, left identity" -}
options :: ParserInfo Options
options = info (opts <**> helper) desc where
  desc = fullDesc <> header "Testing harness for comparing the \
    \Linux Red-Black trees against a verified oracle. The harness immediately stops\
    \ when a kernel RBT violates an RBT invariant and prints an error message."
  opts = Options
    <$> switch (short 'v' <> help "Print all executed operations and the resulting trees.")
    <*> strategies

  randomDesc = progDesc "Randomly call insert and delete operations \
    \on the kernel and oracle without resetting their trees. Will lead to \
    \large trees."

  exhaustiveDesc = progDesc "Try (almost) all combinations up to a limit."

  strategies :: Parser Strategy
  strategies = hsubparser $ mempty
    <> command "random" (info randomOpts randomDesc)
    <> command "exhaustive" (info exhaustiveOpts exhaustiveDesc)

  naturalParser :: (Integral i, Read i) => ReadM i
  naturalParser = eitherReader $ \s ->
    if 0 <= read s
    then Right $ read s
    else Left "Not a positive value"

  numberParser :: (Integral i, Read i) => Parser i
  numberParser = option naturalParser (short 'n')

  randomOpts :: Parser Strategy
  randomOpts = fmap Random $ RandomOptions
    <$> numberParser
    <*> option auto (mempty
          <> short 's'
          <> metavar "<seed>"
          <> showDefault
          <> value 42
          <> help "Seed for the pseudo-random-number generator" )

  exhaustiveOpts :: Parser Strategy
  exhaustiveOpts = Exhaustive <$> ExhaustiveOptions <$> numberParser
