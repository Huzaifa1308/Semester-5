import streamlit as st
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns

from sklearn.model_selection import train_test_split
from sklearn.linear_model import LinearRegression
from sklearn.preprocessing import PolynomialFeatures
from sklearn.dummy import DummyRegressor
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
from sklearn.utils import resample

# ------------------- Streamlit Config -------------------
st.set_page_config(
    page_title="Student Performance Dashboard",
    layout="wide"
)

st.title("📊 Student Marks Prediction Dashboard")
st.write("Interactive Dashboard for CS4048 – Data Science Project")

st.sidebar.header("Upload Cleaned Dataset")
uploaded_file = st.sidebar.file_uploader("Upload Excel or CSV file", type=["xlsx", "csv"])

# ------------------- File Upload -------------------
if uploaded_file is not None:
    if uploaded_file.name.endswith(".csv"):
        df = pd.read_csv(uploaded_file)
    else:
        df = pd.read_excel(uploaded_file)
    st.success("File successfully uploaded!")
else:
    st.warning("Please upload your cleaned dataset to continue.")
    st.stop()

# ------------------- Normalize Column Names -------------------
df.columns = [c.strip().lower() for c in df.columns]

# Map dataset columns to standard names
colmap = {
    "assignment": "assignment",
    "assignments": "assignment",
    "quiz": "quiz",
    "quizzes": "quiz",
    "sessional1": "sessional1",
    "sessinal1": "sessional1",  # typo fix
    "sessional2": "sessional2",
    "sessinal2": "sessional2",  # typo fix
    "final": "final"
}

df = df.rename(columns=colmap)

# ------------------- Column Validation -------------------
required_cols = ["assignment", "quiz", "sessional1", "sessional2", "final"]
missing = [c for c in required_cols if c not in df.columns]

if missing:
    st.error(f"Dataset missing required columns: {missing}")
    st.stop()

# ------------------- Dataset Preview -------------------
st.subheader("📁 Dataset Preview")
st.dataframe(df.head())

# ------------------- EDA -------------------
st.subheader("📈 Exploratory Data Analysis")
col1, col2 = st.columns(2)

with col1:
    st.write("### Correlation Heatmap")
    fig, ax = plt.subplots(figsize=(6,5))
    sns.heatmap(df.corr(), cmap="coolwarm", annot=False, ax=ax)
    st.pyplot(fig)

with col2:
    st.write("### Distribution of Columns")
    selected_col = st.selectbox("Select column to visualize", df.columns)
    fig2, ax2 = plt.subplots()
    sns.histplot(df[selected_col], kde=True, ax=ax2)
    st.pyplot(fig2)

# ------------------- Model Training -------------------
def train_models(X, y):
    results = {}
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=0.2, random_state=42
    )

    # Linear Regression
    lr = LinearRegression()
    lr.fit(X_train, y_train)
    pred_lr = lr.predict(X_test)
    results["Linear Regression"] = {
        "MAE": mean_absolute_error(y_test, pred_lr),
        "RMSE": np.sqrt(mean_squared_error(y_test, pred_lr)),
        "R2": r2_score(y_test, pred_lr)
    }

    # Polynomial Regression
    poly = PolynomialFeatures(2)
    X_train_poly = poly.fit_transform(X_train)
    X_test_poly = poly.transform(X_test)
    pr = LinearRegression()
    pr.fit(X_train_poly, y_train)
    pred_poly = pr.predict(X_test_poly)
    results["Polynomial Regression (Deg 2)"] = {
        "MAE": mean_absolute_error(y_test, pred_poly),
        "RMSE": np.sqrt(mean_squared_error(y_test, pred_poly)),
        "R2": r2_score(y_test, pred_poly)
    }

    # Dummy Model
    dummy = DummyRegressor(strategy="mean")
    dummy.fit(X_train, y_train)
    pred_dummy = dummy.predict(X_test)
    results["Dummy Model"] = {
        "MAE": mean_absolute_error(y_test, pred_dummy),
        "RMSE": np.sqrt(mean_squared_error(y_test, pred_dummy)),
        "R2": r2_score(y_test, pred_dummy)
    }

    return pd.DataFrame(results).T, X_train, X_test, y_train, y_test, poly, pr, lr

# ------------------- Bootstrapping -------------------
def bootstrap_mae(model, X_train, y_train, n=500):
    maes = []
    for _ in range(n):
        X_bs, y_bs = resample(X_train, y_train)
        model.fit(X_bs, y_bs)
        pred = model.predict(X_bs)
        maes.append(mean_absolute_error(y_bs, pred))
    return np.mean(maes), np.percentile(maes, 2.5), np.percentile(maes, 97.5)

# ------------------- Prediction Task -------------------
st.subheader("🎓 Prediction Tasks")
rq = st.selectbox(
    "Select Prediction Task:",
    ["Predict Sessional1", "Predict Sessional2", "Predict Final"]
)

# ------------------- Feature & Target Selection -------------------
if rq == "Predict Sessional1":
    target = "sessional1"
    features = ["assignment", "quiz"]

elif rq == "Predict Sessional2":
    target = "sessional2"
    features = ["sessional1", "assignment", "quiz"]

else:
    target = "final"
    features = ["assignment", "quiz", "sessional1", "sessional2"]

X = df[features]
y = df[target]

# ------------------- Model Evaluation -------------------
st.subheader("📊 Model Evaluation")
results, X_train, X_test, y_train, y_test, poly, model_poly, lr_model = train_models(X, y)
st.write("### Performance Comparison")
st.dataframe(results)

# ------------------- Overfitting Check -------------------
st.subheader("📉 Overfitting Check (Polynomial Model)")
X_train_poly = poly.transform(X_train)
X_test_poly = poly.transform(X_test)
train_pred = model_poly.predict(X_train_poly)
test_pred = model_poly.predict(X_test_poly)

st.write("*Train MAE:*", mean_absolute_error(y_train, train_pred))
st.write("*Test MAE:*", mean_absolute_error(y_test, test_pred))
st.write("*Train R2:*", r2_score(y_train, train_pred))
st.write("*Test R2:*", r2_score(y_test, test_pred))

# ------------------- Bootstrapping -------------------
st.subheader("🔁 Bootstrapping (500 Samples)")
mean_mae, ci_low, ci_high = bootstrap_mae(model_poly, X_train_poly, y_train)
st.write(f"*Bootstrapped MAE Mean:* {mean_mae:.4f}")
st.write(f"*95% CI:* [{ci_low:.4f}, {ci_high:.4f}]")

# ------------------- User Input -------------------
st.subheader("✏ Enter Your Scores for Prediction")
user_input = {}
for feature in features:
    user_input[feature] = st.number_input(
        f"Enter {feature} score",
        min_value=0.0,
        max_value=100.0,
        value=50.0
    )

user_df = pd.DataFrame([user_input])

# ------------------- Prediction -------------------
st.subheader("🤖 Predicted Score for Selected Task")
user_input_poly = poly.transform(user_df)
pred_score_poly = model_poly.predict(user_input_poly)[0]
pred_score_poly = max(0, min(100, pred_score_poly))

pred_score_lr = lr_model.predict(user_df)[0]
pred_score_lr = max(0, min(100, pred_score_lr))

model_choice = st.selectbox("Select Model for Prediction", ["Polynomial Regression", "Linear Regression"])
if model_choice == "Polynomial Regression":
    st.write(f"Predicted *{rq}* score: *{pred_score_poly:.2f}*")
else:
    st.write(f"Predicted *{rq}* score: *{pred_score_lr:.2f}*")

st.success("Dashboard ready!")
