using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class PlayerFlip : MonoBehaviour
{
    public float speed;
    private Rigidbody2D rb;

    private Vector2 MoveAmount;

    private bool FacingRight = true;

   
    void Start()
    {
        rb = GetComponent<Rigidbody2D>();
      
        
    }

    
    void Update()
    {
        Vector2 moveInput = new Vector2(Input.GetAxisRaw("Horizontal"), Input.GetAxisRaw("Vertical"));
        MoveAmount = moveInput.normalized * speed;

        

       
        Vector3 mousePos = Input.mousePosition;
        mousePos = Camera.main.ScreenToWorldPoint(mousePos);

        
        if (mousePos.x < transform.position.x && FacingRight)
        {
            Flip();
        }
        else
        if (mousePos.x > transform.position.x && !FacingRight)
        {
            Flip();
        }

        
    }

    private void FixedUpdate()
    {
        rb.MovePosition(rb.position + MoveAmount * Time.fixedDeltaTime);

    }

    void Flip ()
    {
        
        Vector3 tmpScale = transform.localScale;
        tmpScale.x = - tmpScale.x;
        transform.localScale = tmpScale;
        FacingRight = !FacingRight;

        
    }

}