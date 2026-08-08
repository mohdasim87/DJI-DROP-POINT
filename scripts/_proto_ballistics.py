import math

g = 9.80665

def alpha_for(surface="open_country"):
    return {"open_water": 0.10, "open_country": 0.14, "broken": 0.20, "urban": 0.27}[surface]

def wind_at_height(v_ref, z_ref, z, alpha):
    if z <= 0.5:
        return 0.0
    return v_ref * (z / z_ref) ** alpha

def density(h_asl, temp_c):
    p = 101325.0 * (1 - 2.25577e-5*h_asl)**5.25588
    return p / (287.05*(temp_c+273.15))

def sim(mass, cda, release_agl, gs, heading_deg, wind_speed_ref, wind_from_deg, alpha, temp_c, target_elev, z_ref):
    rho = density(target_elev, temp_c)  # approx constant air density near ground; could vary with u
    # initial velocity: payload carried at aircraft velocity
    heading_rad = math.radians(heading_deg)
    vE = gs*math.sin(heading_rad)
    vN = gs*math.cos(heading_rad)
    vU = 0.0
    # position: origin at release, u = height above target
    e,n,u = 0.0,0.0,release_agl
    t = 0.0
    dt = 0.005
    wind_toward_deg = (wind_from_deg+180)%360
    wt_rad = math.radians(wind_toward_deg)
    def wind_vec(z):
        spd = wind_at_height(wind_speed_ref, z_ref, z, alpha)
        we = spd*math.sin(wt_rad)
        wn = spd*math.cos(wt_rad)
        return we,wn,0.0

    def deriv(state):
        e,n,u,ve,vn,vu = state
        we,wn,wu = wind_vec(u)
        rve, rvn, rvu = ve-we, vn-wn, vu-wu
        speed = math.sqrt(rve**2+rvn**2+rvu**2)
        k = rho*cda/(2*mass)
        ae = -k*speed*rve
        an = -k*speed*rvn
        au = -g - k*speed*rvu
        return (ve,vn,vu, ae,an,au)

    state = (e,n,u,vE,vN,vU)
    while state[2] > 0 and t < 60:
        k1 = deriv(state)
        s2 = tuple(state[i]+dt/2*k1[i] for i in range(6))
        k2 = deriv(s2)
        s3 = tuple(state[i]+dt/2*k2[i] for i in range(6))
        k3 = deriv(s3)
        s4 = tuple(state[i]+dt*k3[i] for i in range(6))
        k4 = deriv(s4)
        newstate = tuple(state[i] + dt/6*(k1[i]+2*k2[i]+2*k3[i]+k4[i]) for i in range(6))
        if newstate[2] <= 0:
            # linear interpolate to u=0
            frac = state[2] / (state[2]-newstate[2])
            e_i = state[0] + frac*(newstate[0]-state[0])
            n_i = state[1] + frac*(newstate[1]-state[1])
            t_i = t + frac*dt
            state = (e_i, n_i, 0.0, newstate[3], newstate[4], newstate[5])
            t = t_i
            break
        state = newstate
        t += dt
    return state[0], state[1], t

# Vector 1 check
e,n,t = sim(0.5, 1e-9, 50, 5, 0, 0, 270, 0.14, 15, 0, 50)
print("V1", e,n,t, math.hypot(e,n))

# Vector 2
e,n,t = sim(0.5, 0.030, 50, 5.0, 0, 5.0, 270, 0.14, 38, 0, 50)
print("V2 zref=50", e,n,t, math.hypot(e,n))

e,n,t = sim(0.5, 0.030, 50, 5.0, 0, 5.0, 270, 0.14, 38, 0, 10)
print("V2 zref=10", e,n,t, math.hypot(e,n))
