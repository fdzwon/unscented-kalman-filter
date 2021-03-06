#include "ukf.h"
#include "tools.h"
#include "Eigen/Dense"
#include <iostream>

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

/**
 * Initializes Unscented Kalman filter
 */
UKF::UKF() {
  // if this is false, laser measurements will be ignored (except during init)

  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  // initial state vector
  x_ = VectorXd(5);

  // initial covariance matrix
  P_ = MatrixXd(5, 5);

  // Process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = 3.0;

  // Process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = .6;

  // Laser measurement noise standard deviation position1 in m
  std_laspx_ = 0.15;

  // Laser measurement noise standard deviation position2 in m
  std_laspy_ = 0.15;

  // Radar measurement noise standard deviation radius in m
  std_radr_ = 0.3;

  // Radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.03;

  // Radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.3;

  //not initialized
  is_initialized_ = false;

  //initialize state vector
  x_ << 0, 0, 0, 0, 0;

  //number of dimension after augmentation for noise
  n_aug_ = 7;

  //state vector dimenmsion
  n_x_ = 5;

  //matrix for sigma points 
  Xsig_pred_ = MatrixXd(n_x_, 2*n_aug_ + 1);

  //constant used for spreading of sigma pts
  lambda_ = 3 - n_aug_;

  //create weighted values for sigma pt spread
  weights_ = VectorXd(2 * n_aug_ + 1);
  weights_.fill(0.5 / (lambda_ + n_aug_));
  weights_(0) = lambda_ / (lambda_ + n_aug_);


  //create matrix with predicted sigma points as columns
  MatrixXd Xsig_pred = MatrixXd(n_x_, 2 * n_aug_ + 1);

  //initialize NIS readings
  NIS_radar_ = 0.0;
  NIS_laser_ = 0.0;

  //initialize cavariance matrix
  P_ << 1, 0, 0, 0, 0,
	  0, 1, 0, 0, 0,
	  0, 0, 1, 0, 0,
	  0, 0, 0, .5, 0,
	  0, 0, 0, 0, 1;


}

UKF::~UKF() {}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(MeasurementPackage meas_package) {
  /**

  Initialize readings and switch between lidar and radar measurements.

  */
	if (!is_initialized_) {

		time_us_ = meas_package.timestamp_;
		if (meas_package.sensor_type_ == MeasurementPackage::LASER && use_laser_) {

			x_(0) = meas_package.raw_measurements_(0);
			x_(1) = meas_package.raw_measurements_(1);
			P_(0, 0) = std_laspx_*std_laspx_; //first measurment process covariance 
			P_(1, 1) = std_laspy_*std_laspy_; //first measurment process covariance
			is_initialized_ = true;
	
		}
		else if (meas_package.sensor_type_ == MeasurementPackage::RADAR && use_radar_) {
			
			x_(0) = meas_package.raw_measurements_(0) *cos(meas_package.raw_measurements_(1));
			x_(1) = meas_package.raw_measurements_(0) *sin(meas_package.raw_measurements_(1));
			if (x_(0) == 0 || x_(1) == 0) {
				return;
			}
			is_initialized_ = true;
		}
		return;
	}
	

	float delta_t = (meas_package.timestamp_ - time_us_) / 1000000.0;
	Prediction(delta_t);
	time_us_ = meas_package.timestamp_;

	if (meas_package.sensor_type_ == MeasurementPackage::LASER && use_laser_) {
		
		UpdateLidar(meas_package);
	}
	else if (meas_package.sensor_type_ == MeasurementPackage::RADAR && use_radar_) {
		
		UpdateRadar(meas_package);

		}
	return;

	}

/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double delta_t) {
	/**
	
	Estimate the object's location. Modify the state
	vector, x_. Predict sigma points, the state, and the state covariance matrix.
	*/

	//create augmented mean vector
	VectorXd x_aug = VectorXd(7);

	//create augmented mean state vector
	x_aug.head(5) = x_;
	x_aug(5) = 0.0;
	x_aug(6) = 0.0;


	//create augmented state covariance matrix
	MatrixXd Xsig_aug = MatrixXd(n_aug_, 2 * n_aug_ + 1);
	Xsig_aug.fill(0);

	//create augmented covariance matrix
	MatrixXd P_aug = MatrixXd(n_aug_, n_aug_);

	P_aug.fill(0.0);
	P_aug.topLeftCorner(5, 5) = P_;
	P_aug(5, 5) = std_a_*std_a_;
	P_aug(6, 6) = std_yawdd_*std_yawdd_;

	MatrixXd L = P_aug.llt().matrixL();

	//create augmented sigma points
	Xsig_aug.col(0) = x_aug;
	for (int i = 0; i < n_aug_; i++) {
		Xsig_aug.col(i + 1) = x_aug + sqrt(lambda_ + n_aug_)*L.col(i);
		Xsig_aug.col(i + 1 + n_aug_) = x_aug - sqrt(lambda_ + n_aug_)*L.col(i);

	}

	for (int i = 0; i < 2 * n_aug_ + 1; i++) {

		//extract values for better readability
		const double p_x = Xsig_aug(0, i);
		const double p_y = Xsig_aug(1, i);
		const double v = Xsig_aug(2, i);
		const double yaw = Xsig_aug(3, i);
		const double yawd = Xsig_aug(4, i);
		const double nu_a = Xsig_aug(5, i);
		const double nu_yawdd = Xsig_aug(6, i);

		//predicted state values
		double px_p, py_p;

		//avoid division by zero
		if (fabs(yawd) > 0.001) {
			px_p = p_x + v / yawd * (sin(yaw + yawd*delta_t) - sin(yaw));
			py_p = p_y + v / yawd * (cos(yaw) - cos(yaw + yawd*delta_t));
		}
		else {
			px_p = p_x + v*delta_t*cos(yaw);
			py_p = p_y + v*delta_t*sin(yaw);
		}

		double v_p = v;
		double yaw_p = yaw + yawd*delta_t;
		double yawd_p = yawd;

		//add noise
		px_p = px_p + 0.5*nu_a*delta_t*delta_t * cos(yaw);
		py_p = py_p + 0.5*nu_a*delta_t*delta_t * sin(yaw);
		v_p = v_p + nu_a*delta_t;

		yaw_p = yaw_p + 0.5*nu_yawdd*delta_t*delta_t;
		yawd_p = yawd_p + nu_yawdd*delta_t;

		//write predicted sigma point into right column
		Xsig_pred_(0, i) = px_p;
		Xsig_pred_(1, i) = py_p;
		Xsig_pred_(2, i) = v_p;
		Xsig_pred_(3, i) = yaw_p;
		Xsig_pred_(4, i) = yawd_p;
	}

	//predict the state mean
	VectorXd x = VectorXd(n_x_);
	x_.fill(0.0);
	for (int i = 0; i < 2 * n_aug_ + 1; i++) {
		x_ = x_ + weights_(i)*Xsig_pred_.col(i);

	}
	//normalize mean vector angle
	x_(3) = atan2(sin(x_(3)), cos(x_(3)));
	//predicted state covariance matrix
	P_.fill(0.0);
	for (int i = 0; i < 2 * n_aug_ + 1; i++) {
		VectorXd x_diff = Xsig_pred_.col(i) - x_;
		//angle normalization
		x_diff(3) = atan2(sin(x_diff(3)), cos(x_diff(3)));

		P_ = P_ + weights_(i) * x_diff * x_diff.transpose();
	}
	

}


/**
 * Updates the state and the state covariance matrix using a laser measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateLidar(MeasurementPackage meas_package) {
  /**
  
  Uses lidar data to update the belief about the object's
  position. Modifies the state vector, x_, and covariance, P_.

  Calculates lidar NIS.

  */

  //measurment matrix
	MatrixXd H = MatrixXd(2, 5);
	H << 1, 0, 0, 0, 0,
		0, 1, 0, 0, 0;
	MatrixXd Ht = H.transpose();
	//measurement noise covariance matrix
	MatrixXd R = MatrixXd(2, 2);
	R << std_laspx_*std_laspx_, 0,
		0, std_laspy_*std_laspy_;
	VectorXd z_pred = H * x_;
	MatrixXd S = H*P_*Ht + R;
	MatrixXd Si = S.inverse();
	MatrixXd PHt = P_*Ht;
	MatrixXd K = PHt*Si;
	VectorXd y = meas_package.raw_measurements_ - z_pred;
	x_ = x_ + (K*y);
	long x_size = x_.size();
	MatrixXd I = MatrixXd::Identity(x_size, x_size);
	P_ = (I - K*H)*P_;

	//calculate NIS
	NIS_laser_ = y.transpose()*Si*y;
 
}

/**
 * Updates the state and the state covariance matrix using a radar measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateRadar(MeasurementPackage meas_package) {

	/**
	
	Uses radar data to update the belief about the object's
	position. Modifies the state vector, x_, and covariance, P_.

	Calculates the radar NIS.

	*/

	//set measurement dimension, radar can measure r, phi, and r_dot

	int n_z = 3;
	//create matrix for sigma points in measurement space
	MatrixXd Zsig = MatrixXd(n_z, 2 * n_aug_ + 1);
	Zsig.fill(0.0);

	//transform sigma points into measurement space
	for (int i = 0; i < 2 * n_aug_ + 1; i++) {

		double p_x = Xsig_pred_(0, i);
		double p_y = Xsig_pred_(1, i);
		double v = Xsig_pred_(2, i);
		double yaw = Xsig_pred_(3, i);

		double v1 = cos(yaw)*v;
		double v2 = sin(yaw)*v;

		// measurement model
		double u = std::max(.001, sqrt(p_x*p_x + p_y*p_y)); // 2 tests for division by zero
		if (fabs(p_x) < .001 && fabs(p_y) < .001) {
			p_x = .001;
		}
		Zsig(0, i) = u;						  //r
		Zsig(1, i) = atan2(p_y, p_x);         //phi
		Zsig(2, i) = (p_x*v1 + p_y*v2) / u;   //r_dot

	}

	//mean predicted measurement
	VectorXd z_pred = VectorXd(n_z);
	z_pred.fill(0.0);
	for (int i = 0; i < 2 * n_aug_ + 1; i++) {
		z_pred = z_pred + weights_(i) * Zsig.col(i);
	}

	//measurement covariance matrix S
	MatrixXd S = MatrixXd(n_z, n_z);
	S.fill(0.0);
	for (int i = 0; i < 2 * n_aug_ + 1; i++) {  //2n+1 simga points
											   //residual
		VectorXd z_diff = Zsig.col(i) - z_pred;

		//angle normalization

		z_diff(1) = atan2(sin(z_diff(1)), cos(z_diff(1)));

		S = S + weights_(i) * z_diff * z_diff.transpose();
	}

	//add measurement noise covariance matrix
	MatrixXd R = MatrixXd(n_z, n_z);
	R << std_radr_*std_radr_, 0, 0,
		0, std_radphi_*std_radphi_, 0,
		0, 0, std_radrd_*std_radrd_;
	S = S + R;

	//create matrix for cross correlation Tc
	MatrixXd Tc = MatrixXd(n_x_, n_z);

	//calculate cross correlation matrix
	Tc.fill(0.0);
	for (int i = 0; i < 2 * n_aug_ + 1; i++) {

		VectorXd z_diff = Zsig.col(i) - z_pred;
		//angle normalization

		z_diff(1) = atan2(sin(z_diff(1)), cos(z_diff(1)));

		// state difference
		VectorXd x_diff = Xsig_pred_.col(i) - x_;
		//angle normalization
		x_diff(3) = atan2(sin(x_diff(3)), cos(x_diff(3)));

		Tc = Tc + weights_(i) * x_diff * z_diff.transpose();
	}

	//Kalman gain K;
	MatrixXd K = Tc * S.inverse();

	VectorXd z = VectorXd(3);
	z = meas_package.raw_measurements_;

	//residual
	VectorXd z_diff = z - z_pred;

	//angle normalization
	z_diff(1) = atan2(sin(z_diff(1)), cos(z_diff(1)));

	//update state mean and covariance matrix
	x_ = x_ + K * z_diff;
	P_ = P_ - K*S*K.transpose();

	//calculate NIS
	NIS_radar_ = z_diff.transpose()*S.inverse()*z_diff;
}
