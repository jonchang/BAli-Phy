module Distributions where
{
import Range;

builtin exponential_density 2 "exponential_density" "Distribution";

builtin builtin_gamma_density 3 "gamma_density" "Distribution";
builtin builtin_gamma_quantile 3 "gamma_quantile" "Distribution";

builtin builtin_beta_density 3 "beta_density" "Distribution";
builtin builtin_beta_quantile 3 "beta_quantile" "Distribution";

builtin builtin_normal_density 3 "normal_density" "Distribution";
builtin builtin_normal_quantile 3 "normal_quantile" "Distribution";

builtin builtin_cauchy_density 3 "cauchy_density" "Distribution";
builtin builtin_laplace_density 3 "laplace_density" "Distribution";
builtin builtin_dirichlet_density 2 "dirichlet_density" "Distribution";
builtin builtin_uniform_density 3 "uniform_density" "Distribution";

builtin builtin_binomial_density 3 "binomial_density" "Distribution";
builtin geometric_density 2 "geometric_density" "Distribution";

builtin builtin_sample_exponential 1 "sample_exponential" "Distribution";
builtin builtin_sample_laplace 2 "sample_laplace" "Distribution";
builtin builtin_sample_uniform 2 "sample_uniform" "Distribution";
builtin builtin_sample_beta 2 "sample_beta" "Distribution";
builtin builtin_sample_gamma 2 "sample_gamma" "Distribution";
builtin builtin_sample_normal 2 "sample_normal" "Distribution";
builtin builtin_sample_cauchy 2 "sample_cauchy" "Distribution";

builtin crp_density 4 "CRP_density" "Distribution";

data ProbDensity = ProbDensity a b c d;
data DiscreteDistribution a = DiscreteDistribution [(Double,a)];
data Random a = Random a;

pairs f (x:y:t) = f x y : pairs f t;
pairs _ t       = t;

foldt f z []  = z;
foldt f _ [x] = x;
foldt f z xs  = foldt f z (pairs f xs);

balanced_product xs = foldt (*) (doubleToLogDouble 1.0) xs;

density (ProbDensity d _ _ _) = d;
quantile (ProbDensity _ q _ _) = q;
sample (ProbDensity _ _ s _) = s;
distRange (ProbDensity _ _ _ r) = r;

strip (IOReturn v) = v;
strip (IOAndPass f g) = let {x = strip f} in x `seq` (strip (g x));
strip (IOAnd f g) = (strip f) `seq` (strip g);
strip (Random x) = unsafePerformIO' x;

distDefaultValue d = strip (sample d);

gammaDensity (a,b) x = builtin_gamma_density a b x;
gammaQuantile (a,b) p = builtin_gamma_quantile a b p;
betaDensity (a,b) x = builtin_beta_density a b x;
betaQuantile (a,b) p = builtin_beta_quantile a b p;
sample_gamma a b = Random (IOAction2 builtin_sample_gamma a b);

sample_beta a b = Random (IOAction2 builtin_sample_beta a b);

normalDensity (mu,sigma) x =  builtin_normal_density mu sigma x;
normalQuantile (mu,sigma) p =  builtin_normal_quantile mu sigma p;
sample_normal m s = Random (IOAction2 builtin_sample_normal m s);

sample_uniform a b = Random (IOAction2 builtin_sample_uniform a b);

cauchyDensity (m,s) x = builtin_cauchy_density m s x;
sample_cauchy m s = Random (IOAction2 builtin_sample_cauchy m s);

laplace_density m s x = builtin_laplace_density m s x;
sample_laplace m s = Random (IOAction2 builtin_sample_laplace m s);

dirichletDensity ps xs = builtin_dirichlet_density (listToVectorDouble ps) (listToVectorDouble xs);

uniformDensity (min,max) x = builtin_uniform_density min max x;

sample_exponential mu = Random (IOAction1 builtin_sample_exponential mu);
exponentialQuantile mu p = gammaQuantile (1.0,mu) p;

mixtureDensity ((p1,dist1):l) x = (doubleToLogDouble p1)*(density dist1 x) + (mixtureDensity l x);
mixtureDensity [] _ = (doubleToLogDouble 0.0);

sample_mixture ((p1,dist1):l) = sample dist1;
sample_dirichlet l = let {n = length l} in return $ replicate n $ 1.0/(intToDouble n);

mixtureRange ((_,dist1):_) = distRange dist1;

bernoulliDensity p b = if b then (doubleToLogDouble p) else (doubleToLogDouble (1.0-p));

bernoulli args = ProbDensity (bernoulliDensity args) (error "Bernoulli has no quantile") (return False) TrueFalseRange;

categorical p = ProbDensity (q!) (error "Categorical has no quantiles") (return 0) (IntegerInterval (Just 0) (Just (length p - 1)))
                where {q = listArray' $ map doubleToLogDouble p};

beta args = ProbDensity (betaDensity args) (betaQuantile args) (return $ (\(a,b)->a/(a+b)) args) (between 0.0 1.0);
uniform (l,u) = ProbDensity (uniformDensity (l,u)) () (sample_uniform l u) (between l u);

normal args = ProbDensity (normalDensity args) (normalQuantile args) (return 0.0) realLine;
exponential mu = ProbDensity (exponential_density mu) (exponentialQuantile mu) (sample_exponential mu) (above 0.0);
gamma args = ProbDensity (gammaDensity args) (gammaQuantile args) (return (\(a,b)->a*b) args) (above 0.0);
laplace (m,s) = ProbDensity (laplace_density m s) () (sample_laplace m s) realLine;
cauchy args = ProbDensity (cauchyDensity args) () (return 0.0) realLine;

logNormal = expTransform' normal;
logExponential = expTransform' exponential;
logGamma = expTransform' gamma;
logLaplace = expTransform' laplace;
logCauchy = expTransform' cauchy;

geometric_quantile p = error "geometric currently has no quantile";

geometric p = ProbDensity (geometric_density p) (geometric_quantile p) (return 1) (IntegerInterval (Just 0) Nothing);

dirichlet args = ProbDensity (dirichletDensity args) (error "Dirichlet has no quantiles") (sample_dirichlet args) (Simplex (length args) 1.0);
dirichlet' (n,x) = dirichlet (replicate n x);

mixture args = ProbDensity (mixtureDensity args) (error "Mixture has no quantiles") (sample_mixture args) (mixtureRange args);

binomial_density (n,p) k = builtin_binomial_density n p k;

binomial (n,p) = ProbDensity (binomial_density (n,p)) (error "binomial has no quantile") (return $ doubleToInt $ (intToDouble n)*p) (IntegerInterval (Just 0) (Just n));

listDensity ds xs = if (length ds == length xs) then pr else (doubleToLogDouble 0.0)
  where {densities = zipWith density ds xs;
         pr = balanced_product densities};

list dists = ProbDensity (listDensity dists) quantiles (mapM sample dists) (ListRange (map distRange dists))
  where { quantiles = (error "list distribution has no quantiles") };

crp (alpha,n,d) = ProbDensity (crp_density alpha n d) quantiles (return $ replicate n 0) (ListRange (replicate n (IntegerInterval (Just 0) (Just (n+d-1)))))
  where { quantiles = (error "crp distribution has no quantiles") };

iid (n,d) = list (replicate n d);

plate (n,f) = list $ map f [0..n-1];
  
fmap1 f [] = [];
fmap1 f ((x,y):l) = (f x,y):(fmap1 f l);
fmap1 f (DiscreteDistribution l) = DiscreteDistribution (fmap1 f l);

fmap2 f [] = [];
fmap2 f ((x,y):l) = (x,f y):(fmap2 f l);
fmap2 f (DiscreteDistribution l) = DiscreteDistribution (fmap2 f l);

extendDiscreteDistribution (DiscreteDistribution d) p x = DiscreteDistribution ((p,x):(fmap1 (\q->q*(1.0-p)) d));

uniformQuantiles q n = map (\i -> q ((2.0*(intToDouble i)+1.0)/(intToDouble n)) ) (take n [1..]);

unwrapDD (DiscreteDistribution l) = l;

mixDiscreteDistributions' (h:t) (h2:t2) = (fmap1 (\q->q*h) h2)++(mixDiscreteDistributions' t t2);
mixDiscreteDistributions' [] [] = [];

mixDiscreteDistributions l1 l2 = DiscreteDistribution (mixDiscreteDistributions' l1 (fmap unwrapDD l2));

average (DiscreteDistribution l) = foldl' (\x y->(x+(fst y)*(snd y))) 0.0 l;

uniformGrid n = DiscreteDistribution [( 1.0/n', (2.0*i'+1.0)/(2.0*n') ) | i <- take n [0..], let {n' = intToDouble n;i'=intToDouble i}];

uniformDiscretize q n = fmap2 q (uniformGrid n);

expTransform (ProbDensity d q s r) = ProbDensity (\x -> (d $ log x)/(doubleToLogDouble x)) (q.log) (do {v <- s; return $ exp v}) (Range.expTransform r);
expTransform' family args = Distributions.expTransform (family args);
}
