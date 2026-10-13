import numpy

X = numpy.array([
    [0.90, 0.10, 0.10],
    [0.80, 0.20, 0.10],
    [0.75, 0.15, 0.20],
    [0.10, 0.85, 0.15],
    [0.20, 0.70, 0.20],
    [0.15, 0.20, 0.90],
    [0.20, 0.15, 0.80],
    [0.30, 0.30, 0.75]
], dtype=float)

# RED = 1, NOT RED = 0
y = numpy.array([1, 1, 1, 0, 0, 0, 0, 0])

w = numpy.zeros(3)
b = 0.0

learning_rate = 0.15
best_loss = 0.0001
lambda_L2 = 0.015

def sigmoid(z: float):
    return 1/(1 + numpy.exp(-z))

def binary_cross_entropy(y, p):
    eps = 1e-12
    p = numpy.clip(p, eps, 1 - eps)
    return -numpy.mean(
        y * numpy.log(p)
        + (1 - y) * numpy.log(1 - p)
    )

def predict_proba(x, w, b: float):
    x = numpy.asarray(x, dtype = float)
    last_loss = 0.0
    for epoch in range(1000):
        z  = x@w+b
        p = sigmoid(z)
        loss = binary_cross_entropy(y, p)
        loss += lambda_L2/2 *numpy.sum(w**2)
        if numpy.all(abs(loss - last_loss) <= best_loss):
            best_weights = w.copy()
            best_bias = b
            return best_weights, best_bias
        else:
            last_loss = loss
        error = p - y
        dw = x.T @ error / len(y) + lambda_L2 * w
        db = numpy.mean(error)
        w -= learning_rate*dw
        b -= learning_rate*db
    return w,b
weights, bias = predict_proba(X, w, b)
print(weights, bias)

