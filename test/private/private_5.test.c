int main() {
  int I, X, Y;
  #pragma sapfor analysis dependency(I, X) shared(Y)
  for (I = 0; I < 10; ++I) {
    if (Y > 0)
      X = I;
    X = X + 1;
  }
  return 0;
}